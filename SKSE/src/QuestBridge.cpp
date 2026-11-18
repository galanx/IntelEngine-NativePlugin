#include "QuestBridge.h"

#include "CellAnalyzer.h"
#include "ItemNames.h"
#include "LivingWorldBridge.h"
#include "LocationResolver.h"
#include "NPCIndex.h"
#include "SkyrimNetAPI.h"
#include "SpeechTracker.h"
#include "StorylineBridge.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace IntelEngine {

    namespace {
        namespace Q = Quests;

        constexpr std::uint32_t kJournalQuestBase = 0x58D2;   // IntelEngine_Quest0..5 in IntelEngine.esp (esp/EspBuilder)
        constexpr const char* kPlugin = "IntelEngine.esp";
        constexpr float kWithDistance = 450.0f;              // "with someone": a few steps away
        constexpr float kSpokeSeconds = 20.0f;               // "while they speak": they said something this long ago
        constexpr float kGiverRestDays = 7.0f;               // a giver whose quest ended asks again after a week at least
        constexpr float kMemoryDays = 30.0f;                 // an ended quest stays in the giver's bio this long
        constexpr std::string_view kRumorLabel = "IntelEngine quest: ";

        RE::BGSLocation* HoldOf(RE::BGSLocation* location) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                if (loc->HasKeywordString("LocTypeHold")) return loc;
            }
            return nullptr;
        }

        bool Within(RE::BGSLocation* location, RE::FormID place) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                if (loc->GetFormID() == place) return true;
            }
            return false;
        }

        // Where an actor is, as a location: their cell's, else the one they last were in.
        RE::BGSLocation* LocationOf(RE::TESObjectREFR* ref) {
            if (!ref) return nullptr;
            auto* cell = ref->GetParentCell();
            RE::BGSLocation* loc = cell ? cell->GetLocation() : nullptr;
            if (!loc) loc = ref->GetCurrentLocation();
            return loc;
        }

        bool InTamriel(RE::TESObjectREFR* ref) {
            int depth = 0;
            for (auto* world = ref ? ref->GetWorldspace() : nullptr; world && depth < 4; world = world->parentWorld, ++depth) {
                const char* id = world->GetFormEditorID();
                if (id && _stricmp(id, "Tamriel") == 0) return true;
            }
            return false;
        }

        // The map marker of the town, dungeon or hold a location belongs to, in Tamriel.
        std::optional<RE::NiPoint3> MarkerOf(RE::BGSLocation* location) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                auto marker = std::as_const(loc->worldLocMarker).get();
                if (marker && InTamriel(marker.get())) return marker->GetPosition();
            }
            return std::nullopt;
        }

        std::string NameOf(RE::FormID id) {
            if (!id) return {};
            auto* form = RE::TESForm::LookupByID(id);
            if (!form) return {};
            if (auto* ref = form->As<RE::TESObjectREFR>()) {
                const char* name = ref->GetDisplayFullName();
                return name ? name : "";
            }
            const char* name = form->GetName();
            return name ? name : "";
        }

        // A place's name in the middle of a sentence ("the Bannered Mare", "Bleak Falls Barrow").
        std::string PlaceName(RE::FormID location) {
            auto* loc = location ? RE::TESForm::LookupByID<RE::BGSLocation>(location) : nullptr;
            const char* name = loc ? loc->GetFullName() : nullptr;
            if (!name || !*name) return "a place nearby";
            std::string text = name;
            if (text.starts_with("The ")) text[0] = 't';
            return text;
        }

        std::string PlayerName() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const char* name = player ? player->GetDisplayFullName() : nullptr;
            return name && *name ? name : "the traveller";
        }

        // The player is right here with this actor: same space, within the distance.
        bool PlayerIsWith(RE::Actor* actor, float distance) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !actor || !actor->Is3DLoaded()) return false;
            auto* cell = actor->GetParentCell();
            auto* playerCell = player->GetParentCell();
            if (!cell || !playerCell) return false;
            if (cell->IsInteriorCell() || playerCell->IsInteriorCell()) {
                if (cell != playerCell) return false;
            } else if (actor->GetWorldspace() != player->GetWorldspace()) {
                return false;
            }
            return actor->GetPosition().GetDistance(player->GetPosition()) < distance;
        }

        bool SpokeJustNow(RE::FormID actor) {
            const float since = SpeechTracker::GetSingleton()->SecondsSinceSpeechEnded(actor);
            return since >= 0.0f && since <= kSpokeSeconds;
        }

        // The stamped item in someone's inventory: the entry of that base form whose extra data carries the stamp.
        struct Stamped {
            RE::TESBoundObject* object = nullptr;
            RE::ExtraDataList* extra = nullptr;
        };
        Stamped FindStamped(RE::TESObjectREFR* holder, RE::FormID item, const std::string& stamp) {
            Stamped out;
            if (!holder || !item || stamp.empty()) return out;
            auto* changes = holder->GetInventoryChanges();
            if (!changes || !changes->entryList) return out;
            for (auto* entry : *changes->entryList) {
                if (!entry || !entry->object || entry->object->GetFormID() != item || !entry->extraLists) continue;
                for (auto* extra : *entry->extraLists) {
                    if (!extra) continue;
                    auto* text = extra->GetByType<RE::ExtraTextDisplayData>();
                    const char* name = text ? text->displayName.c_str() : nullptr;
                    if (name && stamp == name) {
                        out.object = entry->object;
                        out.extra = extra;
                        return out;
                    }
                }
            }
            return out;
        }

        std::string Escape(const std::string& text) {
            std::string out;
            for (const char c : text) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        }

        std::string Ago(float days) {
            if (days < 0.05f) return "just now";
            if (days < 0.5f) return "earlier today";
            if (days < 1.5f) return "a day ago";
            if (days < 4.0f) return "a few days ago";
            if (days < 10.0f) return "about a week ago";
            return "some time ago";
        }

        RE::TESQuest* JournalQuest(std::uint8_t slot) {
            if (slot >= Q::kSlots) return nullptr;
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::TESQuest>(kJournalQuestBase + slot, kPlugin) : nullptr;
        }

        // Test hook (QuestDebugForceCompanion): 0 as chance has it, 1 a follower type the player never travelled with,
        // 2 another former follower, 3 alone.
        std::atomic<int> g_forceCompanion{0};

        // Facts that only the main thread may read, kept for the bio decorator (SkyrimNet's threads).
        struct Seen {
            bool hasItem = false;
        };
    }

    float QuestBridge::NowDays() {
        auto* calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetCurrentGameTime() : 0.0f;
    }

    // ------------------------------------------------------------------ the main menu

    void QuestBridge::Catalogue() {
        if (m_catalogued) return;
        m_catalogued = true;
        const auto started = std::chrono::steady_clock::now();
        std::vector<Place> places;
        for (const auto& [locationId, anchorId] : LocationResolver::GetSingleton()->DungeonAnchors()) {
            auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(locationId);
            if (!location || !anchorId) continue;
            const char* name = location->GetFullName();
            if (!name || !*name || location->HasKeywordString("LocTypePlayerHouse")) continue;
            if (CellAnalyzer::GetSingleton()->IsLocationNonCombative(location)) continue;
            Place p;
            p.location = locationId;
            p.anchor = anchorId;
            auto* hold = HoldOf(location);
            p.hold = hold ? hold->GetFormID() : 0;
            if (const auto at = MarkerOf(location)) {
                p.at = *at;
                p.hasAt = true;
            }
            p.crypt = location->HasKeywordString("LocTypeDraugrCrypt");
            places.push_back(p);
        }
        std::vector<RE::FormID> keepsakes;
        std::vector<RE::FormID> modded;   // used only when the base game has none (plain names: "Silver Necklace")
        if (auto* data = RE::TESDataHandler::GetSingleton()) {
            // Plain jewellery is what enchanted jewellery is made from (its template): "Gold Ring", not "Ring of Corpse
            // Curse". Read from the records, so it holds in any language.
            std::unordered_set<RE::FormID> plain;
            for (auto* armor : data->GetFormArray<RE::TESObjectARMO>()) {
                if (armor && armor->templateArmor && armor->HasKeywordString("ArmorJewelry")) plain.insert(armor->templateArmor->GetFormID());
            }
            for (auto* armor : data->GetFormArray<RE::TESObjectARMO>()) {
                if (!armor || !armor->HasKeywordString("ArmorJewelry")) continue;
                if (!plain.empty() && !plain.contains(armor->GetFormID())) continue;
                if (armor->GetFormFlags() & RE::TESObjectARMO::RecordFlags::kNonPlayable) continue;
                if (armor->formEnchanting || armor->templateArmor) continue;
                const char* name = armor->GetName();
                if (!name || !*name) continue;
                if (armor->value < 25 || armor->value > 1500) continue;
                auto* file = armor->GetFile(0);
                const std::string_view origin = file ? file->GetFilename() : std::string_view();
                const bool baseGame = origin == "Skyrim.esm" || origin == "Update.esm" || origin == "Dawnguard.esm" ||
                                      origin == "HearthFires.esm" || origin == "Dragonborn.esm";
                (baseGame ? keepsakes : modded).push_back(armor->GetFormID());
            }
        }
        if (keepsakes.empty()) keepsakes = std::move(modded);
        // Follower types: everyone the game lets the player hire or befriend as a follower (the potential follower
        // faction, mods' followers included), by their placed reference. A venture's ally may have fallen in with one
        // the player never travelled with.
        std::vector<RE::FormID> followerTypes;
        if (auto* data = RE::TESDataHandler::GetSingleton()) {
            auto* potential = data->LookupForm<RE::TESFaction>(0x0005C84D, "Skyrim.esm");
            for (auto* npc : data->GetFormArray<RE::TESNPC>()) {
                if (!npc || !potential || !npc->IsUnique()) continue;
                bool member = false;
                for (const auto& f : npc->factions) {
                    if (f.faction == potential && f.rank >= 0) member = true;
                }
                const char* name = npc->GetFullName();
                if (!member || !name || !*name) continue;
                const auto refId = NPCIndex::GetSingleton()->FindFormIdByName(name);
                auto* ref = refId ? RE::TESForm::LookupByID<RE::Actor>(refId) : nullptr;
                if (!ref || ref->GetActorBase() != npc) continue;   // a fuzzy match or an unplaced base: not them
                followerTypes.push_back(refId);
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        logger::info("Quests: {} places with a boss room, {} keepsakes and {} follower types, {:.1f} ms at the main menu", places.size(),
                     keepsakes.size(), followerTypes.size(), ms);
        std::scoped_lock lock(m_mutex);
        m_places = std::move(places);
        m_keepsakes = std::move(keepsakes);
        m_followerTypes = std::move(followerTypes);
    }

    // ------------------------------------------------------------------ planning

    std::uint32_t QuestBridge::PlanFor(RE::Actor* giver) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!giver || !player || giver->IsDead() || giver->IsChild() || giver->IsPlayerTeammate()) {
            logger::info("Quests: no quest for {:08X}: not someone who can ask", giver ? giver->GetFormID() : 0);
            return 0;
        }
        if (player->IsInCombat()) {
            logger::info("Quests: no quest now: the player is fighting");
            return 0;
        }
        const float now = NowDays();
        const auto giverId = giver->GetFormID();
        auto* giverLoc = LocationOf(giver);
        auto* giverHold = HoldOf(giverLoc);
        const auto giverAt = MarkerOf(giverLoc);
        auto* playerLoc = LocationOf(player);
        const auto playerPlace = playerLoc ? playerLoc->GetFormID() : 0;
        if (StorylineBridge::GetSingleton()->OpenIdFor(giverId) != 0) {
            logger::info("Quests: no quest for {}: they have an errand open with the player", giver->GetDisplayFullName());
            return 0;
        }
        std::scoped_lock lock(m_mutex);
        if (const auto why = m_book.Refusal(giverId); !why.empty()) {
            logger::info("Quests: no quest for {}: {}", giver->GetDisplayFullName(), why);
            return 0;
        }
        for (const auto& q : m_book.all) {
            if (q.giver == giverId && q.phase == Q::Phase::Ended && q.ending != Q::Ending::Lapsed && now - q.since < kGiverRestDays) {
                logger::info("Quests: no quest for {}: their last one ended {:.1f} days ago", giver->GetDisplayFullName(), now - q.since);
                return 0;
            }
        }
        std::vector<Q::PlaceOption> options;
        options.reserve(m_places.size());
        for (const auto& p : m_places) {
            auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(p.location);
            Q::PlaceOption o;
            o.location = p.location;
            o.anchor = p.anchor;
            o.inGiverHold = giverHold && p.hold == giverHold->GetFormID();
            o.cleared = location && location->IsCleared();
            o.crypt = p.crypt;
            o.distance = (p.hasAt && giverAt) ? p.at.GetDistance(*giverAt) : 0.0f;
            options.push_back(o);
        }
        const std::uint32_t seed = Q::detail::Mix(giverId, static_cast<std::uint32_t>(now * 24.0f));
        const auto* place = Q::PickPlace(options, m_book, playerPlace, seed);
        if (!place) {
            logger::info("Quests: no quest for {}: no place to send the player", giver->GetDisplayFullName());
            return 0;
        }
        const auto tmpl = Q::PickTemplate(!m_keepsakes.empty(), true, m_book.lastTemplate, seed);
        Q::Quest q;
        q.tmpl = tmpl;
        q.giver = giverId;
        q.place = place->location;
        q.anchor = place->anchor;
        q.seed = seed;
        q.truth = Q::PickTruth(tmpl, *place, false, seed);
        q.gold = Q::RewardGold(tmpl, player->GetLevel());
        if (tmpl == Q::Template::Keepsake) {
            q.item = m_keepsakes[Q::detail::Mix(seed, 53u) % m_keepsakes.size()];
            const std::string itemName = ItemNames::Display(NameOf(q.item));
            q.stamp = Q::ShortName(giver->GetDisplayFullName()) + "'s " + itemName;
        }
        if (tmpl == Q::Template::Bounty) {
            // Named now so the giver can say who; the sex is the foe's, known once placed (SetFoe may rename).
            q.foeName = Q::FoeName(seed, Q::FoeFemale(seed));
        }
        q.title = Fill(Q::DefaultTitle(tmpl), q);
        const auto id = m_book.Plan(q, now);
        if (id) {
            logger::info("Quests: #{} planned: {} for {} at {} ({}), truth {}, item '{}', {} gold", id, static_cast<int>(tmpl),
                         giver->GetDisplayFullName(), PlaceName(q.place), place->inGiverHold ? "their hold" : "another hold",
                         static_cast<int>(q.truth), q.stamp, q.gold);
        }
        return id;
    }

    std::uint32_t QuestBridge::PlanVenture(RE::Actor* ally) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!ally || !player || ally->IsDead() || ally->IsPlayerTeammate() || player->IsInCombat()) {
            logger::info("Quests: no venture for {:08X} now", ally ? ally->GetFormID() : 0);
            return 0;
        }
        const auto allyId = ally->GetFormID();
        auto* lives = LivingWorldBridge::GetSingleton();
        if (!lives->HasOwnLife(allyId)) {
            logger::info("Quests: no venture for {}: not a former follower living a life of their own", ally->GetDisplayFullName());
            return 0;
        }
        const float now = NowDays();
        // Who they travel with, if anyone: another former follower who is free, or a follower type the player may never
        // have travelled with (a hireling, an adventurer of the load order), free to leave where they are, unseen.
        const std::uint32_t pickSeed = Q::detail::Mix(allyId, static_cast<std::uint32_t>(now * 24.0f) + 13u);
        RE::FormID companion = 0;
        const int force = g_forceCompanion.load();
        if (force == 1 || force == 2 || (force == 0 && pickSeed % 100u < 65u)) {
            const RE::FormID former = lives->FreeCompanionFor(allyId);
            std::vector<RE::FormID> strangers;
            {
                std::scoped_lock lock(m_mutex);
                for (const auto id : m_followerTypes) {
                    if (id == allyId || id == former || m_book.LiveFor(id)) continue;
                    strangers.push_back(id);
                }
            }
            std::erase_if(strangers, [](RE::FormID id) {
                auto* a = RE::TESForm::LookupByID<RE::Actor>(id);
                return !a || !LivingWorldBridge::FreeToTravel(a);
            });
            if (force == 1) companion = strangers.empty() ? 0 : strangers[Q::detail::Mix(pickSeed, 3u) % strangers.size()];
            else if (force == 2) companion = former;
            else if (former && (strangers.empty() || pickSeed % 2u == 0u)) companion = former;
            else if (!strangers.empty()) companion = strangers[Q::detail::Mix(pickSeed, 3u) % strangers.size()];
        }
        auto* playerLoc = LocationOf(player);
        auto* playerHold = HoldOf(playerLoc);
        std::optional<RE::NiPoint3> playerAt;
        if (!player->GetParentCell() || player->GetParentCell()->IsInteriorCell() || !InTamriel(player)) playerAt = MarkerOf(playerLoc);
        else playerAt = player->GetPosition();
        std::scoped_lock lock(m_mutex);
        if (const auto why = m_book.Refusal(allyId); !why.empty()) {
            logger::info("Quests: no venture for {}: {}", ally->GetDisplayFullName(), why);
            return 0;
        }
        for (const auto& q : m_book.all) {
            if ((q.giver == allyId || q.ally == allyId) && q.phase == Q::Phase::Ended && q.ending != Q::Ending::Lapsed &&
                now - q.since < kGiverRestDays) {
                logger::info("Quests: no venture for {}: their last one ended {:.1f} days ago", ally->GetDisplayFullName(), now - q.since);
                return 0;
            }
        }
        std::vector<Q::PlaceOption> options;
        for (const auto& p : m_places) {
            auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(p.location);
            Q::PlaceOption o;
            o.location = p.location;
            o.anchor = p.anchor;
            o.inGiverHold = playerHold && p.hold == playerHold->GetFormID();   // met on the road near the player
            o.cleared = location && location->IsCleared();
            o.crypt = p.crypt;
            o.distance = (p.hasAt && playerAt) ? p.at.GetDistance(*playerAt) : 0.0f;
            options.push_back(o);
        }
        const std::uint32_t seed = Q::detail::Mix(allyId, static_cast<std::uint32_t>(now * 24.0f) + 7u);
        const auto* place = Q::PickPlace(options, m_book, playerLoc ? playerLoc->GetFormID() : 0, seed);
        if (!place) {
            logger::info("Quests: no venture for {}: no place to set out for", ally->GetDisplayFullName());
            return 0;
        }
        Q::Quest q;
        q.tmpl = Q::Template::Venture;
        q.giver = allyId;
        q.ally = allyId;
        if (companion && !m_book.LiveFor(companion)) q.companion = companion;
        q.place = place->location;
        q.anchor = place->anchor;
        q.seed = seed;
        q.truth = Q::PickTruth(Q::Template::Venture, *place, false, seed);
        q.gold = Q::RewardGold(Q::Template::Venture, player->GetLevel());
        q.title = Fill(Q::DefaultTitle(Q::Template::Venture), q);
        const auto id = m_book.Plan(q, now);
        if (id) {
            logger::info("Quests: #{} planned: a venture of {} to {}{}, truth {}, {} gold", id, ally->GetDisplayFullName(),
                         PlaceName(q.place), q.companion ? " with " + NameOf(q.companion) : std::string(), static_cast<int>(q.truth), q.gold);
        }
        return id;
    }

    bool QuestBridge::Involves(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        return actor && m_book.LiveFor(actor) != nullptr;
    }

    void QuestBridge::Placed(std::uint32_t id) {
        std::string allyMemory, companionMemory, place;
        RE::FormID ally = 0, companion = 0;
        {
            std::scoped_lock lock(m_mutex);
            const auto* q = m_book.Find(id);
            if (!q || q->tmpl != Q::Template::Venture || !q->companion) return;
            ally = q->ally;
            companion = q->companion;
            allyMemory = Fill(Q::FellInMemory(false), *q);
            companionMemory = Fill(Q::FellInMemory(true), *q);
            place = PlaceName(q->place);
        }
        logger::info("Quests: #{} on the road: {:08X} and {:08X} travel together", id, ally, companion);
        std::thread([ally, companion, allyMemory, companionMemory, place]() {
            if (!SkyrimNetAPI::AddMemory) return;
            const auto related = [](RE::FormID other) { return "[" + std::to_string(other) + "]"; };
            SkyrimNetAPI::AddMemory(ally, allyMemory.c_str(), 0.55f, "EXPERIENCE", "", place.c_str(), "[\"intelengine_quest\"]", related(companion).c_str());
            SkyrimNetAPI::AddMemory(companion, companionMemory.c_str(), 0.55f, "EXPERIENCE", "", place.c_str(), "[\"intelengine_quest\"]", related(ally).c_str());
        }).detach();
    }

    bool QuestBridge::Accept(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        if (!m_book.Accept(id, NowDays())) {
            logger::info("Quests: #{} could not be taken on", id);
            return false;
        }
        const auto* q = m_book.Find(id);
        logger::info("Quests: #{} taken on, journal slot {}, until day {:.2f}", id, static_cast<int>(q->slot), q->deadline);
        Spread(*q);
        return true;
    }

    bool QuestBridge::Decline(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        if (!m_book.Decline(id, NowDays())) return false;
        logger::info("Quests: #{} declined", id);
        Ended(*m_book.Find(id));
        return true;
    }

    std::vector<std::uint32_t> QuestBridge::Lapse() {
        std::scoped_lock lock(m_mutex);
        auto ids = m_book.Lapse(NowDays());
        for (const auto id : ids) logger::info("Quests: #{} lapsed: its offer never came", id);
        return ids;
    }

    std::vector<std::uint32_t> QuestBridge::OpenIds() const {
        std::scoped_lock lock(m_mutex);
        return m_book.OpenIds();
    }

    std::uint32_t QuestBridge::PlannedFor(RE::FormID giver) const {
        std::scoped_lock lock(m_mutex);
        for (const auto& q : m_book.all) {
            if (q.phase == Q::Phase::Planned && q.giver == giver) return q.id;
        }
        return 0;
    }

    // ------------------------------------------------------------------ facts

    namespace {
        std::unordered_map<std::uint32_t, Seen> g_seen;   // by quest id, under QuestBridge's lock
    }

    std::int32_t QuestBridge::Advance(std::uint32_t id) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;
        std::scoped_lock lock(m_mutex);
        auto* q = m_book.Find(id);
        if (!q || q->phase != Q::Phase::Open) return 0;
        Q::Facts f;
        auto* giver = RE::TESForm::LookupByID<RE::Actor>(q->giver);
        auto* foe = q->foe ? RE::TESForm::LookupByID<RE::Actor>(q->foe) : nullptr;
        auto* ally = q->ally ? RE::TESForm::LookupByID<RE::Actor>(q->ally) : nullptr;
        auto* anchor = q->anchor ? RE::TESForm::LookupByID<RE::TESObjectREFR>(q->anchor) : nullptr;
        auto* here = LocationOf(player);
        f.inPlace = Within(here, q->place);
        if (!f.inPlace && anchor && anchor->Is3DLoaded() && anchor->GetParentCell() == player->GetParentCell()) {
            f.inPlace = anchor->GetPosition().GetDistance(player->GetPosition()) < 4000.0f;
        }
        if (ally) f.allyInPlace = Within(LocationOf(ally), q->place);
        f.hasItem = q->item && !q->stamp.empty() && FindStamped(player, q->item, q->stamp).object != nullptr;
        f.handedOver = q->handed;
        f.foeDead = foe && foe->IsDead();
        if (q->tmpl == Q::Template::Venture) {
            auto* place = RE::TESForm::LookupByID<RE::BGSLocation>(q->place);
            f.cleared = foe ? foe->IsDead() : (place && place->IsCleared());
        }
        f.withGiver = giver && PlayerIsWith(giver, kWithDistance);
        f.giverSpoke = giver && SpokeJustNow(q->giver);
        f.withAlly = ally && PlayerIsWith(ally, kWithDistance);
        f.allySpoke = ally && SpokeJustNow(q->ally);
        f.giverDead = giver && giver->IsDead();
        f.allyDead = ally && ally->IsDead();
        g_seen[id].hasItem = f.hasItem;
        const auto change = Q::Advance(*q, f, NowDays());
        if (!change.any()) return 0;
        std::int32_t bits = 0;
        for (const auto step : change.done) bits |= 1 << step;
        logger::info("Quests: #{} steps done {:#x}, now at step {}{}", id, bits, static_cast<int>(q->step),
                     change.ending != Q::Ending::None ? ", ended " + std::to_string(static_cast<int>(change.ending)) : std::string());
        if (change.ending != Q::Ending::None) Ended(*q);
        return bits | (static_cast<std::int32_t>(change.ending) << 8);
    }

    bool QuestBridge::MayHandOver(std::uint32_t id) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::scoped_lock lock(m_mutex);
        const auto* q = m_book.Find(id);
        if (!q || q->phase != Q::Phase::Open || q->handed || !player) return false;
        const auto steps = Q::StepsOf(q->tmpl);
        if (q->step >= steps.size() || steps[q->step].kind != Q::StepKind::HandOver) return false;
        auto* giver = RE::TESForm::LookupByID<RE::Actor>(q->giver);
        if (!giver || giver->IsDead() || !PlayerIsWith(giver, kWithDistance) || !SpokeJustNow(q->giver)) return false;
        if (!FindStamped(player, q->item, q->stamp).object) return false;
        const float real = static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
        if (m_handOverOfferedAt >= 0.0f && real - m_handOverOfferedAt < 60.0f) return false;
        m_handOverOfferedAt = real;
        return true;
    }

    bool QuestBridge::HandOver(std::uint32_t id) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::scoped_lock lock(m_mutex);
        auto* q = m_book.Find(id);
        if (!q || q->phase != Q::Phase::Open || q->handed || !player) return false;
        auto* giver = RE::TESForm::LookupByID<RE::Actor>(q->giver);
        if (!giver || giver->IsDead()) return false;
        const auto found = FindStamped(player, q->item, q->stamp);
        if (!found.object) {
            logger::info("Quests: #{} handover refused: '{}' is not on the player", id, q->stamp);
            return false;
        }
        player->RemoveItem(found.object, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, found.extra, giver);
        q->handed = true;
        logger::info("Quests: #{} '{}' handed to {}", id, q->stamp, giver->GetDisplayFullName());
        return true;
    }

    // ------------------------------------------------------------------ fields for the scripts

    int QuestBridge::Field(std::uint32_t id, const std::string& name) const {
        std::scoped_lock lock(m_mutex);
        const auto* q = m_book.Find(id);
        if (!q) return -1;
        const auto steps = Q::StepsOf(q->tmpl);
        auto lower = name;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "slot") return q->slot == 0xFF ? -1 : q->slot;
        if (lower == "step") return q->step;
        if (lower == "steps") return static_cast<int>(steps.size());
        if (lower == "phase") return static_cast<int>(q->phase);
        if (lower == "ending") return static_cast<int>(q->ending);
        if (lower == "template") return static_cast<int>(q->tmpl);
        if (lower == "truth") return static_cast<int>(q->truth);
        if (lower == "placed") return q->placed;
        if (lower == "gold") return static_cast<int>(q->gold);
        if (lower == "openstage") return Q::OpenStage(q->tmpl);
        if (lower == "endstage") return Q::EndStage(q->tmpl, q->ending);
        if (lower == "foefemale") return Q::FoeFemale(q->seed) ? 1 : 0;
        if (lower == "crypt") {
            for (const auto& p : m_places) if (p.location == q->place) return p.crypt ? 1 : 0;
            return 0;
        }
        if (lower == "objective") return q->step < steps.size() ? steps[q->step].objective : -1;
        // "objective<n>", "log<n>": of step n
        if (lower.starts_with("objective") || lower.starts_with("log")) {
            const bool log = lower.starts_with("log");
            const auto n = std::atoi(lower.c_str() + (log ? 3 : 9));
            if (n < 0 || static_cast<std::size_t>(n) >= steps.size()) return -1;
            return log ? steps[static_cast<std::size_t>(n)].logStage : steps[static_cast<std::size_t>(n)].objective;
        }
        return -1;
    }

    RE::TESForm* QuestBridge::Form(std::uint32_t id, const std::string& name) const {
        std::scoped_lock lock(m_mutex);
        const auto* q = m_book.Find(id);
        if (!q) return nullptr;
        auto lower = name;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        auto form = [](RE::FormID f) { return f ? RE::TESForm::LookupByID(f) : nullptr; };
        if (lower == "giver") return form(q->giver);
        if (lower == "foe") return form(q->foe);
        if (lower == "ally") return form(q->ally);
        if (lower == "companion") return form(q->companion);
        if (lower == "place") return form(q->place);
        if (lower == "anchor") return form(q->anchor);
        if (lower == "item") return form(q->item);
        if (lower == "holder") return form(q->holder ? q->holder : q->foe);
        if (lower == "marker") {
            const auto steps = Q::StepsOf(q->tmpl);
            if (q->step >= steps.size()) return nullptr;
            switch (steps[q->step].marker) {
                case Q::Role::Giver: return form(q->giver);
                case Q::Role::Foe: return form(q->foe ? q->foe : q->anchor);
                case Q::Role::Ally: return form(q->ally);
                case Q::Role::Holder: return form(q->holder ? q->holder : q->foe ? q->foe : q->anchor);
                case Q::Role::Anchor: return form(q->anchor);
                default: return nullptr;
            }
        }
        return nullptr;
    }

    std::string QuestBridge::Fill(std::string text, const Q::Quest& q) const {
        const std::string companion = NameOf(q.companion);
        auto* giverActor = RE::TESForm::LookupByID<RE::Actor>(q.giver);
        auto* giverBase = giverActor ? giverActor->GetActorBase() : nullptr;
        const std::string their = !giverBase ? "their" : giverBase->GetSex() == RE::SEX::kFemale ? "her" : "his";
        const std::pair<const char*, std::string> parts[] = {
            {"{player}", PlayerName()},
            {"{givershort}", Q::ShortName(NameOf(q.giver))},
            {"{giver}", NameOf(q.giver)},
            {"{their}", their},
            {"{foe}", q.foeName.empty() ? std::string("a brigand") : q.foeName},
            {"{ally}", NameOf(q.ally)},
            {"{companion}", companion},
            {"{withcompanion}", companion.empty() ? std::string() : " with " + companion},
            {"{place}", PlaceName(q.place)},
            {"{itembase}", ItemNames::Display(NameOf(q.item))},
            {"{item}", q.stamp},
            {"{gold}", std::to_string(q.gold)},
        };
        for (const auto& [key, value] : parts) text = Q::Fill(std::move(text), key, value);
        return text;
    }

    std::string QuestBridge::Text(std::uint32_t id, const std::string& name) const {
        std::scoped_lock lock(m_mutex);
        const auto* q = m_book.Find(id);
        if (!q) return {};
        auto lower = name;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "stamp") return q->stamp;
        if (lower == "title") return q->title;
        if (lower == "foename") return q->foeName;
        if (lower == "narration") return Fill(Q::OfferNarration(*q), *q);
        // A venture is met on the road: what drives them, as the road encounter's narration ("<name> is on the way...").
        if (lower == "roadnarration") {
            return Fill("is on the way to {place}{withcompanion} to clear it out; on recognizing {player} on the road, stops to ask "
                        "{player} to come along and share whatever they find there", *q);
        }
        // Met from behind: they saw the player ahead on the road they were walking and caught up.
        if (lower == "roadcatchup") {
            return Fill("catches up with {player} on the road, having recognized {player} from behind; is on the way to {place}"
                        "{withcompanion} to clear it out, and asks {player} to come along and share whatever they find there", *q);
        }
        // The companion stops with them: glad to see an old friend, or meeting a stranger their friend knows.
        if (lower == "companionnarration") return Fill("stops on the road with {ally}, glad to see {player} again, and is going with {ally} to clear out {place}", *q);
        if (lower == "companionstranger") {
            return Fill("stops on the road with {ally} and has never met {player}; fell in with {ally} a few days ago and is going "
                        "with {ally} to clear out {place}", *q);
        }
        if (lower == "prompt") return Fill(Q::OfferPrompt(*q), *q);
        if (lower == "memory") return Fill(Q::EndMemory(*q), *q);
        if (lower == "place") return PlaceName(q->place);
        if (lower == "itembase") return ItemNames::Display(NameOf(q->item));
        return {};
    }

    std::string QuestBridge::SetFoe(std::uint32_t id, RE::Actor* foe) {
        std::scoped_lock lock(m_mutex);
        auto* q = m_book.Find(id);
        if (!q || !foe) return {};
        q->foe = foe->GetFormID();
        q->holder = q->foe;
        auto* base = foe->GetActorBase();
        if (q->foeName.empty()) q->foeName = Q::FoeName(q->seed, base && base->GetSex() == RE::SEX::kFemale);
        // The name goes on the reference (SetDisplayName in the script) and into the title; the journal's alias
        // names read the template's ("Bandit Chief"), so no objective names them.
        q->title = Fill(Q::DefaultTitle(q->tmpl), *q);
        logger::info("Quests: #{} foe placed: {:08X}, named {}", id, q->foe, q->foeName);
        return q->foeName;
    }

    void QuestBridge::SetPlaced(std::uint32_t id, int bits) {
        std::scoped_lock lock(m_mutex);
        if (auto* q = m_book.Find(id)) q->placed = static_cast<std::uint8_t>(q->placed | (bits & 0xFF));
    }

    void QuestBridge::ApplyTitles() const {
        std::scoped_lock lock(m_mutex);
        for (const auto& q : m_book.all) {
            if (q.slot == 0xFF || q.title.empty()) continue;
            if (auto* quest = JournalQuest(q.slot)) quest->fullName = q.title.c_str();
        }
    }

    // ------------------------------------------------------------------ what people know

    std::string QuestBridge::LinesFor(RE::Actor* actor) const {
        if (!actor) return {};
        const auto id = actor->GetFormID();
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        std::string out;
        for (const auto& q : m_book.all) {
            if (q.companion == id && q.tmpl == Q::Template::Venture) {
                if (Q::IsLive(q) || (q.ending != Q::Ending::Lapsed && now - q.since < kMemoryDays)) {
                    const auto line = Q::CompanionLines(q);
                    if (!line.empty()) out += "- " + Fill(line, q) + "\n";
                }
                continue;
            }
            if (q.giver != id && q.ally != id) continue;
            std::string line;
            if (Q::IsLive(q)) {
                const auto seen = g_seen.find(q.id);
                line = Q::GiverLines(q, seen != g_seen.end() && seen->second.hasItem);
                if (!line.empty() && q.phase == Q::Phase::Open) line += " (" + Ago(now - q.opened) + ")";
            } else if (q.ending != Q::Ending::Lapsed && now - q.since < kMemoryDays) {
                line = Q::EndMemory(q);
                if (!line.empty()) line += " (" + Ago(now - q.since) + ")";
            }
            if (!line.empty()) out += "- " + Fill(line, q) + "\n";
        }
        return out;
    }

    std::string QuestBridge::BusinessLine(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        for (const auto& q : m_book.all) {
            if (!Q::IsLive(q) || (q.giver != actor && q.ally != actor)) continue;
            auto line = Q::GiverLines(q, false);
            // First person to third: the line is about the candidate.
            line = Q::Fill(std::move(line), "I asked ", "asked ");
            line = Q::Fill(std::move(line), "I am going to ask ", "is going to ask ");
            line = Q::Fill(std::move(line), "My ", "Their ");
            line = Q::Fill(std::move(line), " my ", " their ");
            line = Q::Fill(std::move(line), " me ", " them ");
            line = Q::Fill(std::move(line), " I ", " they ");
            return Fill(line, q);
        }
        return {};
    }

    void QuestBridge::Spread(const Q::Quest& q) {
        const auto text = Q::Rumor(q);
        auto* giverLoc = LocationOf(RE::TESForm::LookupByID<RE::Actor>(q.giver));
        auto* hold = HoldOf(giverLoc);
        const char* holdName = hold ? hold->GetFullName() : nullptr;
        if (text.empty() || !holdName || !*holdName || !SkyrimNetAPI::AddWorldKnowledge) return;
        const std::string rumor = Fill(text, q);
        const std::string giverName = NameOf(q.giver);
        const std::string condition = "get_intelengine_hold(actorUUID) == \"" + Escape(holdName) + "\" and actorName != \"" + Escape(giverName) + "\"";
        const std::string label = std::string(kRumorLabel) + std::to_string(q.id);
        const auto id = q.id;
        std::thread([this, rumor, condition, label, id]() {
            const int knowledge = SkyrimNetAPI::AddWorldKnowledge(rumor.c_str(), condition.c_str(), false, 0.5f, label.c_str());
            logger::info("Quests: #{} heard in the hold ({}): {}", id, knowledge, rumor);
            if (knowledge <= 0) return;
            bool kept = false;
            {
                std::scoped_lock lock(m_mutex);
                if (auto* q = m_book.Find(id); q && q->phase == Q::Phase::Open && q->rumor == 0) {
                    q->rumor = knowledge;
                    kept = true;
                }
            }
            if (!kept && SkyrimNetAPI::RemoveWorldKnowledge) SkyrimNetAPI::RemoveWorldKnowledge(knowledge);
        }).detach();
    }

    void QuestBridge::Ended(const Q::Quest& q) {
        const int openRumor = q.rumor;
        const std::string memory = Fill(Q::EndMemory(q), q);
        const std::string companionMemory = q.companion ? Fill(Q::CompanionLines(q), q) : std::string();
        const auto companion = q.companion;
        const std::string deed = Fill(Q::Rumor(q), q);
        const auto who = q.tmpl == Q::Template::Venture ? q.ally : q.giver;
        auto* giverLoc = LocationOf(RE::TESForm::LookupByID<RE::Actor>(who));
        auto* hold = HoldOf(giverLoc);
        const std::string holdName = hold && hold->GetFullName() ? hold->GetFullName() : "";
        const std::string giverName = NameOf(who);
        const std::string place = PlaceName(q.place);
        const auto id = q.id;
        logger::info("Quests: #{} ended ({}): {}", id, static_cast<int>(q.ending), memory);
        std::thread([openRumor, memory, deed, who, holdName, giverName, place, id, companion, companionMemory]() {
            if (openRumor > 0 && SkyrimNetAPI::RemoveWorldKnowledge) SkyrimNetAPI::RemoveWorldKnowledge(openRumor);
            if (companion && !companionMemory.empty() && SkyrimNetAPI::AddMemory) {
                SkyrimNetAPI::AddMemory(companion, companionMemory.c_str(), 0.6f, "EXPERIENCE", "", place.c_str(), "[\"intelengine_quest\"]", "[]");
            }
            if (!memory.empty() && who && SkyrimNetAPI::AddMemory) {
                const int m = SkyrimNetAPI::AddMemory(who, memory.c_str(), 0.7f, "EXPERIENCE", "", place.c_str(), "[\"intelengine_quest\"]", "[]");
                logger::info("Quests: #{} memory {} for {:08X}", id, m, who);
            }
            if (!deed.empty() && !holdName.empty() && SkyrimNetAPI::AddWorldKnowledge) {
                const std::string condition = "get_intelengine_hold(actorUUID) == \"" + Escape(holdName) + "\" and actorName != \"" + Escape(giverName) + "\"";
                const std::string label = "IntelEngine deed: " + giverName;
                const int k = SkyrimNetAPI::AddWorldKnowledge(deed.c_str(), condition.c_str(), false, 0.45f, label.c_str());
                logger::info("Quests: #{} deed heard in the hold ({}): {}", id, k, deed);
            }
        }).detach();
    }

    // ------------------------------------------------------------------ save data

    void QuestBridge::Save(SKSE::SerializationInterface* intfc) {
        std::vector<std::uint8_t> bytes;
        {
            std::scoped_lock lock(m_mutex);
            bytes = Q::Serialize(m_book);
        }
        if (!intfc->OpenRecord(Q::kRecordType, Q::kRecordVersion)) {
            logger::error("Quests: could not open the co-save record");
            return;
        }
        intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }

    void QuestBridge::Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version != Q::kRecordVersion || length > (1u << 20)) {
            logger::warn("Quests: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("Quests: the co-save record is short; starting without quests");
            return;
        }
        Q::Book book;
        if (!Q::Deserialize(bytes, book, version)) {
            logger::warn("Quests: the co-save record is damaged; starting without quests");
            return;
        }
        const auto resolve = [intfc](std::uint32_t& formID) {
            if (formID == 0) return true;
            RE::FormID resolved = 0;
            if (!intfc->ResolveFormID(formID, resolved)) {
                formID = 0;
                return false;
            }
            formID = resolved;
            return true;
        };
        std::erase_if(book.all, [&](Q::Quest& q) {
            if (!resolve(q.giver) || q.giver == 0) return true;   // the giver is gone from the load order
            for (auto* form : {&q.foe, &q.ally, &q.companion, &q.place, &q.anchor, &q.item, &q.third, &q.holder}) resolve(*form);
            return false;
        });
        std::scoped_lock lock(m_mutex);
        m_book = std::move(book);
        g_seen.clear();
        logger::info("Quests: loaded {} ({} open)", m_book.all.size(), m_book.Count(Q::Phase::Open));
    }

    void QuestBridge::Revert() {
        std::scoped_lock lock(m_mutex);
        m_book = Q::Book{};
        g_seen.clear();
    }

    // ------------------------------------------------------------------ natives (script IntelEngine)

    namespace QuestNatives {
        namespace {
            std::int32_t QuestPlanFor(RE::StaticFunctionTag*, RE::Actor* giver) {
                return static_cast<std::int32_t>(QuestBridge::GetSingleton()->PlanFor(giver));
            }
            std::int32_t QuestPlanVenture(RE::StaticFunctionTag*, RE::Actor* ally) {
                return static_cast<std::int32_t>(QuestBridge::GetSingleton()->PlanVenture(ally));
            }
            bool QuestAccept(RE::StaticFunctionTag*, std::int32_t id) {
                const bool ok = QuestBridge::GetSingleton()->Accept(static_cast<std::uint32_t>(id));
                if (ok) QuestBridge::GetSingleton()->ApplyTitles();
                return ok;
            }
            bool QuestDecline(RE::StaticFunctionTag*, std::int32_t id) { return QuestBridge::GetSingleton()->Decline(static_cast<std::uint32_t>(id)); }
            std::vector<std::int32_t> ToInts(const std::vector<std::uint32_t>& ids) {
                return std::vector<std::int32_t>(ids.begin(), ids.end());
            }
            std::vector<std::int32_t> QuestLapse(RE::StaticFunctionTag*) { return ToInts(QuestBridge::GetSingleton()->Lapse()); }
            std::vector<std::int32_t> QuestOpenIds(RE::StaticFunctionTag*) { return ToInts(QuestBridge::GetSingleton()->OpenIds()); }
            std::int32_t QuestPlannedFor(RE::StaticFunctionTag*, RE::Actor* giver) {
                return giver ? static_cast<std::int32_t>(QuestBridge::GetSingleton()->PlannedFor(giver->GetFormID())) : 0;
            }
            std::int32_t QuestAdvance(RE::StaticFunctionTag*, std::int32_t id) { return QuestBridge::GetSingleton()->Advance(static_cast<std::uint32_t>(id)); }
            bool QuestMayHandOver(RE::StaticFunctionTag*, std::int32_t id) { return QuestBridge::GetSingleton()->MayHandOver(static_cast<std::uint32_t>(id)); }
            bool QuestHandOver(RE::StaticFunctionTag*, std::int32_t id) { return QuestBridge::GetSingleton()->HandOver(static_cast<std::uint32_t>(id)); }
            std::int32_t QuestField(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) {
                return QuestBridge::GetSingleton()->Field(static_cast<std::uint32_t>(id), name.c_str() ? name.c_str() : "");
            }
            RE::TESForm* QuestForm(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) {
                return QuestBridge::GetSingleton()->Form(static_cast<std::uint32_t>(id), name.c_str() ? name.c_str() : "");
            }
            RE::BSFixedString QuestText(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) {
                return QuestBridge::GetSingleton()->Text(static_cast<std::uint32_t>(id), name.c_str() ? name.c_str() : "").c_str();
            }
            RE::BSFixedString QuestSetFoe(RE::StaticFunctionTag*, std::int32_t id, RE::Actor* foe) {
                return QuestBridge::GetSingleton()->SetFoe(static_cast<std::uint32_t>(id), foe).c_str();
            }
            void QuestSetPlaced(RE::StaticFunctionTag*, std::int32_t id, std::int32_t bits) {
                QuestBridge::GetSingleton()->SetPlaced(static_cast<std::uint32_t>(id), bits);
            }
            void QuestApplyTitles(RE::StaticFunctionTag*) { QuestBridge::GetSingleton()->ApplyTitles(); }
            void QuestVenturePlaced(RE::StaticFunctionTag*, std::int32_t id) { QuestBridge::GetSingleton()->Placed(static_cast<std::uint32_t>(id)); }
            void QuestDebugForceCompanion(RE::StaticFunctionTag*, std::int32_t mode) { g_forceCompanion.store(mode); }
        }

        void Register(RE::BSScript::IVirtualMachine* a_vm, int& count) {
            constexpr auto script = "IntelEngine";
            a_vm->RegisterFunction("QuestPlanFor", script, QuestPlanFor); ++count;
            a_vm->RegisterFunction("QuestPlanVenture", script, QuestPlanVenture); ++count;
            a_vm->RegisterFunction("QuestAccept", script, QuestAccept); ++count;
            a_vm->RegisterFunction("QuestDecline", script, QuestDecline); ++count;
            a_vm->RegisterFunction("QuestLapse", script, QuestLapse); ++count;
            a_vm->RegisterFunction("QuestOpenIds", script, QuestOpenIds); ++count;
            a_vm->RegisterFunction("QuestPlannedFor", script, QuestPlannedFor); ++count;
            a_vm->RegisterFunction("QuestAdvance", script, QuestAdvance); ++count;
            a_vm->RegisterFunction("QuestMayHandOver", script, QuestMayHandOver); ++count;
            a_vm->RegisterFunction("QuestHandOver", script, QuestHandOver); ++count;
            a_vm->RegisterFunction("QuestField", script, QuestField); ++count;
            a_vm->RegisterFunction("QuestForm", script, QuestForm); ++count;
            a_vm->RegisterFunction("QuestText", script, QuestText); ++count;
            a_vm->RegisterFunction("QuestSetFoe", script, QuestSetFoe); ++count;
            a_vm->RegisterFunction("QuestSetPlaced", script, QuestSetPlaced); ++count;
            a_vm->RegisterFunction("QuestApplyTitles", script, QuestApplyTitles); ++count;
            a_vm->RegisterFunction("QuestVenturePlaced", script, QuestVenturePlaced); ++count;
            a_vm->RegisterFunction("QuestDebugForceCompanion", script, QuestDebugForceCompanion); ++count;
        }
    }
}
