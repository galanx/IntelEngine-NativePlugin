#include "ArcBridge.h"

#include "CellAnalyzer.h"
#include "ItemNames.h"
#include "LivingWorldBridge.h"
#include "LocationResolver.h"
#include "NPCIndex.h"
#include "QuestBridge.h"
#include "SkyrimNetAPI.h"
#include "SpeechTracker.h"

#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

namespace IntelEngine {

    namespace {
        namespace A = Arcs;

        // "with someone": in talking distance. Typed talk reaches farther than a few steps: Ysolda, back at her stall
        // 600 units off, answered and the hand-over never came at 450 (2026-11-18).
        constexpr float kWithDistance = 700.0f;
        constexpr float kSpokeSeconds = 20.0f;     // they said something this long ago
        constexpr float kTalkSeconds = 60.0f;      // and the player said something to someone this long ago
        constexpr float kMemoryDays = 30.0f;       // an ended arc stays in its people's bios this long
        constexpr float kHoldSeconds = 120.0f;     // "not now": the popup waits this long before it asks again
        constexpr float kAskerRestDays = 7.0f;     // who asked does not ask again before this (arcs and quests alike)
        // Pacing (docs/research/PACING.md): a town's story when the player has lingered in that town a while, never
        // sooner than a day after the last one began, one coming at a time; tried at most every couple of hours.
        constexpr float kTownLingerHours = 2.0f;
        constexpr float kArcGapDays = 1.0f;
        constexpr float kPaceRetryDays = 2.0f / 24.0f;
        constexpr std::string_view kRumorLabel = "IntelEngine arc: ";

        // What a role must be free of (Usable).
        constexpr int kStays = 0;      // the asker, a witness, a rival: they only talk
        constexpr int kJailable = 1;   // a culprit: may be taken by the guard for a few days
        constexpr int kBrawler = 2;    // a rival: may end up in a fistfight with the player
        constexpr int kKillable = 3;   // a murder's victim: never anyone the world or a quest needs

        // Skyrim.esm factions whose people are never a culprit or a victim (the jarl, the steward, a merchant, an
        // innkeeper, a bard: the town needs them where they are) or whom the player may have travelled with.
        constexpr RE::FormID kJobJarl = 0x50920;
        constexpr RE::FormID kJobSteward = 0x50922;
        constexpr RE::FormID kJobMerchant = 0x51596;
        constexpr RE::FormID kJobInnkeeper = 0x5091B;
        constexpr RE::FormID kBard = 0x53514;
        constexpr RE::FormID kPotentialFollower = 0x5C84D;
        constexpr RE::FormID kCurrentFollower = 0x5C84E;

        float RealSeconds() {
            return static_cast<float>(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        RE::BGSLocation* HoldOf(RE::BGSLocation* location) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                if (loc->HasKeywordString("LocTypeHold")) return loc;
            }
            return nullptr;
        }

        RE::BGSLocation* TownOf(RE::BGSLocation* location) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                // Cities and towns: a farm or a mill is no town to have a thief among its people, and the Dark
                // Brotherhood's sanctuaries, marked town and dwelling at once, are none either.
                if ((loc->HasKeywordString("LocTypeCity") || loc->HasKeywordString("LocTypeTown")) && !loc->HasKeywordString("LocTypeDwelling")) {
                    return loc;
                }
            }
            return nullptr;
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

        std::string PlaceName(RE::FormID location) {
            auto* loc = location ? RE::TESForm::LookupByID<RE::BGSLocation>(location) : nullptr;
            const char* name = loc ? loc->GetFullName() : nullptr;
            return name && *name ? name : "town";
        }

        std::string PlayerName() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const char* name = player ? player->GetDisplayFullName() : nullptr;
            return name && *name ? name : "the traveller";
        }

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

        // They spoke a moment ago, in a talk the player is part of (the player said something too): an arc's talk is
        // done only when the player talked with them, never by their chatter while the player walked past.
        bool SpokeWithPlayer(RE::FormID actor) {
            auto* speech = SpeechTracker::GetSingleton();
            const float since = speech->SecondsSinceSpeechEnded(actor);
            const float player = speech->SecondsSincePlayerSpoke();
            return since >= 0.0f && since <= kSpokeSeconds && player >= 0.0f && player <= kTalkSeconds;
        }

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

        std::string ShortName(std::string_view name) {
            const auto the = name.find(" the ");
            if (the != std::string_view::npos && the > 0) return std::string(name.substr(0, the));
            return std::string(name);
        }

        RE::TESQuest* JournalQuest(std::uint8_t slot) {
            if (slot >= A::kSlots) return nullptr;
            const std::string editorId = "IntelEngine_Arc" + std::to_string(slot);
            return RE::TESForm::LookupByEditorID<RE::TESQuest>(editorId);
        }

        RE::TESFaction* Faction(RE::FormID id) {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::TESFaction>(id, "Skyrim.esm") : nullptr;
        }

        bool InFaction(RE::Actor* actor, RE::FormID id) {
            auto* faction = Faction(id);
            return faction && actor->IsInFaction(faction);
        }

        // An owner (an actor base, or a faction) that is this actor or one of their factions.
        bool OwnedBy(RE::TESForm* owner, RE::Actor* actor) {
            if (!owner || !actor) return false;
            if (auto* npc = owner->As<RE::TESNPC>()) return npc == actor->GetActorBase();
            if (auto* faction = owner->As<RE::TESFaction>()) return actor->IsInFaction(faction);
            return false;
        }

        // The quests that hold this actor in an alias now, which keep them out of an arc's cast: IntelEngine's own (an
        // errand, a quest, a storyline) and the marriage always; a quest whose shown objective points at them; and,
        // for someone the story takes away (strict: a culprit), any quest the player is on that is not a mere favour.
        // Quests of no journal type and favours run from the start with half a town in their aliases (WITavern holds
        // every innkeeper): they keep nobody out (Hulda, and every Whiterun culprit, 2026-11-18). Quests not started
        // yet that name someone are the catalogue's (m_journalAliases).
        std::string HeldBy(RE::Actor* actor, bool strict) {
            auto* aliases = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (!aliases) return {};
            RE::BSReadLockGuard guard(aliases->lock);
            for (auto* instance : aliases->aliases) {
                if (!instance || !instance->quest || !instance->alias) continue;
                auto* quest = instance->quest;
                const char* raw = quest->GetFormEditorID();
                const std::string_view id = raw ? raw : "";
                const std::string name = id.empty() ? std::string("a quest") : std::string(id);
                if (id.starts_with("IntelEngine_Lives")) continue;   // a former follower's keep slot: checked apart
                // The journal quests of arcs and quests keep their people in their aliases after the story ended (Ysolda,
                // two stories later, was "held by IntelEngine_Arc0"); whether a story is live is the books' to say
                // (Involves), not the aliases'.
                if (id.starts_with("IntelEngine_Arc") || id.starts_with("IntelEngine_Quest")) continue;
                if (id.starts_with("IntelEngine")) return name;
                if (id.starts_with("RelationshipMarriage")) return name;
                const auto type = quest->GetType();
                if (!quest->IsRunning() || type == RE::QUEST_DATA::Type::kNone) continue;
                bool onIt = false;
                for (auto* objective : quest->objectives) {
                    if (!objective) continue;
                    const auto state = objective->state.get();
                    if (state == RE::QUEST_OBJECTIVE_STATE::kDisplayed || state == RE::QUEST_OBJECTIVE_STATE::kCompletedDisplayed ||
                        state == RE::QUEST_OBJECTIVE_STATE::kFailedDisplayed) {
                        onIt = true;
                    }
                    if (state != RE::QUEST_OBJECTIVE_STATE::kDisplayed || !objective->targets) continue;
                    for (std::uint32_t i = 0; i < objective->numTargets; ++i) {
                        auto* target = objective->targets[i];
                        if (target && target->alias == instance->alias->aliasID) return name;
                    }
                }
                if (strict && onIt && type != RE::QUEST_DATA::Type::kMiscellaneous) return name;
            }
            return {};
        }
    }

    float ArcBridge::NowDays() {
        auto* calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetCurrentGameTime() : 0.0f;
    }

    // ------------------------------------------------------------------ the catalogue

    void ArcBridge::Catalogue() {
        if (m_catalogued) return;
        m_catalogued = true;
        const auto started = std::chrono::steady_clock::now();
        // Every unique, grown-up person the director knows (the persistent references), with their home: the interior
        // of the bed they own when the home index has it, else the interior they were placed in by the game's data
        // (a house, a shop, the inn they keep: where their things are). The bed index alone knew 20 people of
        // Whiterun, nearly all with work the town needs, and no thief could be cast there (2026-11-18).
        std::unordered_map<RE::FormID, RE::FormID> bedHome;
        for (const auto& [baseId, cellId] : LocationResolver::GetSingleton()->HomeCells()) bedHome.emplace(baseId, cellId);
        std::vector<Folk> folk;
        std::unordered_map<RE::FormID, int> perTown;
        std::unordered_set<RE::FormID> bases;
        for (const auto id : NPCIndex::GetSingleton()->DirectorActorIds()) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            auto* base = actor ? actor->GetActorBase() : nullptr;
            if (!base || !base->IsUnique() || actor->IsChild() || !bases.insert(base->GetFormID()).second) continue;
            const char* name = actor->GetDisplayFullName();
            if (!name || !*name) continue;
            RE::FormID home = 0;
            RE::BGSLocation* where = nullptr;
            RE::FormID placedIn = 0;
            {
                RE::NiPoint3 pos, rot;
                RE::TESForm* worldOrCell = nullptr;
                actor->GetEditorLocation2(pos, rot, worldOrCell, nullptr);
                if (auto* cell = worldOrCell ? worldOrCell->As<RE::TESObjectCELL>() : nullptr; cell && cell->IsInteriorCell()) {
                    placedIn = cell->GetFormID();
                }
            }
            if (const auto bed = bedHome.find(base->GetFormID()); bed != bedHome.end()) {
                auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(bed->second);
                home = bed->second;
                where = cell ? cell->GetLocation() : nullptr;
            } else {
                RE::NiPoint3 pos, rot;
                RE::TESForm* worldOrCell = nullptr;
                actor->GetEditorLocation2(pos, rot, worldOrCell, nullptr);
                auto* cell = worldOrCell ? worldOrCell->As<RE::TESObjectCELL>() : nullptr;
                if (cell && cell->IsInteriorCell()) {
                    // Their home only when it is theirs (owned by them or their household): the game's data puts
                    // Ahlam and Nazeem in Elrindir's shop, and nothing of hers is there to hide a circlet in
                    // (2026-11-18). Else they still belong to the town, without a home of their own on record.
                    if (OwnedBy(cell->GetOwner(), actor)) home = cell->GetFormID();
                    where = cell->GetLocation();
                }
            }
            if (!where) where = actor->GetEditorLocation1();
            auto* town = TownOf(where);
            if (!town) continue;
            folk.push_back({id, base->GetFormID(), home, town->GetFormID(), home != 0 && home == placedIn});
            ++perTown[town->GetFormID()];
        }
        // Who the quests of the journal name (forced references and unique actors): a culprit is never one of them while
        // that quest is not done, so no story takes away someone a quest the player has not finished still needs.
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> named;
        std::size_t aliasCount = 0;
        if (auto* data = RE::TESDataHandler::GetSingleton()) {
            for (auto* quest : data->GetFormArray<RE::TESQuest>()) {
                // Favours (miscellaneous) and quests of no type are left out: they name half a town and never end.
                if (!quest || quest->GetType() == RE::QUEST_DATA::Type::kNone || quest->GetType() == RE::QUEST_DATA::Type::kMiscellaneous) continue;
                for (auto* alias : quest->aliases) {
                    if (!alias || alias->GetVMTypeID() != RE::BGSRefAlias::VMTYPEID) continue;
                    auto* refAlias = static_cast<RE::BGSRefAlias*>(alias);
                    RE::TESNPC* npc = nullptr;
                    const auto fill = refAlias->fillType.get();
                    if (fill == RE::BGSBaseAlias::FILL_TYPE::kUniqueActor) {
                        npc = refAlias->fillData.uniqueActor.uniqueActor;
                    } else if (fill == RE::BGSBaseAlias::FILL_TYPE::kForced) {
                        auto forced = refAlias->fillData.forced.forcedRef.get();
                        auto* actor = forced ? forced->As<RE::Actor>() : nullptr;
                        npc = actor ? actor->GetActorBase() : nullptr;
                    }
                    if (!npc) continue;
                    auto& list = named[npc->GetFormID()];
                    if (std::find(list.begin(), list.end(), quest->GetFormID()) == list.end()) list.push_back(quest->GetFormID());
                    ++aliasCount;
                }
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        std::string towns;
        for (const auto& [town, count] : perTown) towns += (towns.empty() ? "" : ", ") + PlaceName(town) + " " + std::to_string(count);
        logger::info("Arcs: {} townsfolk with a home ({}), {} actors named by {} aliases of journal quests, {:.1f} ms", folk.size(), towns,
                     named.size(), aliasCount, ms);
        std::scoped_lock lock(m_mutex);
        m_folk = std::move(folk);
        m_journalAliases = std::move(named);
    }

    const ArcBridge::Folk* ArcBridge::FolkOf(RE::FormID ref) const {
        for (const auto& f : m_folk) {
            if (f.ref == ref) return &f;
        }
        return nullptr;
    }

    bool ArcBridge::Usable(RE::Actor* actor, int need, std::string& why) const {
        if (!actor || actor->IsDead() || actor->IsDisabled() || actor->IsDeleted()) return why = "dead or gone", false;
        if (actor->IsChild()) return why = "a child", false;
        if (actor->IsPlayerTeammate() || InFaction(actor, kCurrentFollower)) return why = "with the player", false;
        if (actor->IsInCombat()) return why = "fighting", false;
        // Each of these takes its own lock: Usable is never called with this bridge's lock held.
        if (Involves(actor->GetFormID())) return why = "in another arc", false;
        if (QuestBridge::GetSingleton()->Involves(actor->GetFormID())) return why = "in a quest of IntelEngine", false;
        if (LivingWorldBridge::GetSingleton()->HasOwnLife(actor->GetFormID())) return why = "a former follower with a life of their own", false;
        if (auto held = HeldBy(actor, need == kJailable || need == kKillable); !held.empty()) return why = "held by " + held, false;
        if (need == kBrawler) {
            // Nobody old, no guard, no jarl or steward gets into a fistfight over a town quarrel.
            auto* race = actor->GetRace();
            const auto raceId = race ? race->GetFormID() : 0u;
            if (raceId == 0x00067CD8 || raceId == 0x000A82BA) return why = "too old to brawl", false;
            if (actor->IsGuard()) return why = "a guard", false;
            for (const auto job : {kJobJarl, kJobSteward}) {
                if (InFaction(actor, job)) return why = "the jarl's court", false;
            }
        }
        if (need == kKillable) {
            // Only out of sight: they are taken home and killed where nobody, the player least of all, can see it.
            if (actor->Is3DLoaded()) return why = "where the player could see", false;
        }
        if (need == kJailable || need == kKillable) {
            if (actor->IsEssential() || actor->IsProtected()) return why = "essential", false;
            if (actor->IsGuard() || actor->GetVendorFaction()) return why = "a guard or a merchant", false;
            for (const auto job : {kJobJarl, kJobSteward, kJobMerchant, kJobInnkeeper, kBard, kPotentialFollower}) {
                if (InFaction(actor, job)) return why = "the town needs them (their work)", false;
            }
            auto* base = actor->GetActorBase();
            if (const auto it = base ? m_journalAliases.find(base->GetFormID()) : m_journalAliases.end(); it != m_journalAliases.end()) {
                for (const auto questId : it->second) {
                    auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questId);
                    if (quest && !quest->IsCompleted()) {
                        const char* id = quest->GetFormEditorID();
                        return why = std::string("named by ") + (id && *id ? id : "a quest"), false;
                    }
                }
            }
            auto* crime = actor->GetCrimeFaction();
            if (need == kJailable && (!crime || !crime->crimeData.factionJailMarker)) return why = "no jail in their hold", false;
        }
        return true;
    }

    // ------------------------------------------------------------------ planning

    bool ArcBridge::Cast(A::Arc& a, RE::Actor* asker, std::string& why) const {
        const Folk* me = FolkOf(asker->GetFormID());
        if (!me) return why = "not one of the townsfolk with a home", false;
        a.place = me->town;
        a.cast[A::RAsker] = me->ref;
        auto* askerBase = asker->GetActorBase();
        // Townsfolk of the same town in other homes, in an order of the seed (never the same culprit first).
        std::vector<const Folk*> others;
        for (const auto& f : m_folk) {
            if (f.town == me->town && f.ref != me->ref && (!f.home || f.home != me->home)) others.push_back(&f);
        }
        std::sort(others.begin(), others.end(), [&](const Folk* x, const Folk* y) {
            return A::detail::Mix(a.seed, x->ref) < A::detail::Mix(a.seed, y->ref);
        });
        // How they stand with the asker (the game's relationships): rivals and foes first; friends, family and lovers
        // never steal from them or quarrel with them.
        bool close = false;   // standing scored for someone close to the asker (a murder's victim), else for a foe
        const auto standing = [&](const Folk* f) {
            auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(f->base);
            auto* rel = (npc && askerBase) ? RE::BGSRelationship::GetRelationship(askerBase, npc) : nullptr;
            if (!rel) return 0;
            using L = RE::BGSRelationship::RELATIONSHIP_LEVEL;
            const auto level = rel->level.get();
            const bool dear = level == L::kLover || level == L::kAlly || level == L::kConfidant || level == L::kFriend;
            if (close) return dear ? 3 : 0;
            if (level == L::kLover || level == L::kAlly || level == L::kConfidant) return -100;
            if (level == L::kFriend) return -1;
            if (level == L::kRival || level == L::kFoe || level == L::kEnemy || level == L::kArchnemesis) return 3;
            return 0;
        };
        const auto pick = [&](int need, std::initializer_list<RE::FormID> notHomes, bool byStanding, bool needsHome) -> const Folk* {
            const Folk* best = nullptr;
            int bestScore = -1000;
            std::string reasons;
            for (const auto* f : others) {
                if (f->home && std::find(notHomes.begin(), notHomes.end(), f->home) != notHomes.end()) continue;
                if (needsHome && !f->home) continue;
                if (need == kKillable && !f->placedHome) continue;   // found dead at home: the game must know it as theirs
                bool cast = false;
                for (const auto who : a.cast) cast = cast || who == f->ref;
                if (cast) continue;
                const int score = byStanding ? standing(f) : 0;
                if (score <= -100 || score <= bestScore) {
                    if (score <= -100) reasons += NameOf(f->ref) + " (close to " + NameOf(me->ref) + "), ";
                    continue;
                }
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(f->ref);
                std::string reason;
                if (!Usable(actor, need, reason)) {
                    reasons += NameOf(f->ref) + " (" + reason + "), ";
                    continue;
                }
                best = f;
                bestScore = score;
            }
            if (!best) logger::info("Arcs: nobody of {} fits: {}", PlaceName(me->town), reasons.empty() ? "nobody else lives there" : reasons);
            return best;
        };
        switch (a.pattern) {
            case A::Pattern::Thief: {
                const Folk* culprit = pick(kJailable, {me->home}, true, true);
                if (!culprit) return why = "nobody in town could be the thief", false;
                a.cast[A::RCulprit] = culprit->ref;
                const Folk* witness = pick(kStays, {me->home, culprit->home}, false, false);
                if (!witness) return why = "nobody in town could have seen it", false;
                a.cast[A::RWitness] = witness->ref;
                const auto keepsakes = QuestBridge::GetSingleton()->Keepsakes();
                if (keepsakes.empty()) return why = "no keepsake in the load order", false;
                a.propBase[0] = keepsakes[A::detail::Mix(a.seed, 53u) % keepsakes.size()];
                a.propName[0] = ShortName(asker->GetDisplayFullName()) + "'s " + ItemNames::Display(NameOf(a.propBase[0]));
                return true;
            }
            case A::Pattern::Murder: {
                close = true;
                const Folk* victim = pick(kKillable, {me->home}, true, true);
                close = false;
                if (!victim) return why = "nobody in town could be the victim", false;
                a.cast[A::RVictim] = victim->ref;
                a.scene = victim->home;
                const Folk* culprit = pick(kJailable, {me->home, victim->home}, false, true);
                if (!culprit) return why = "nobody in town could be the killer", false;
                a.cast[A::RCulprit] = culprit->ref;
                const Folk* witness = pick(kStays, {me->home, victim->home, culprit->home}, false, false);
                if (!witness) return why = "nobody in town could have seen it", false;
                a.cast[A::RWitness] = witness->ref;
                const Folk* second = pick(kStays, {me->home, victim->home, culprit->home, witness->home}, false, false);
                if (!second) return why = "too few people in town to suspect", false;
                a.cast[A::RSuspect2] = second->ref;
                const Folk* third = pick(kStays, {me->home, victim->home, culprit->home, witness->home, second->home}, false, false);
                if (!third) return why = "too few people in town to suspect", false;
                a.cast[A::RSuspect3] = third->ref;
                // The weapon, hidden at the killer's home; and a gem of the victim's, still in the killer's pocket.
                auto* data = RE::TESDataHandler::GetSingleton();
                auto* dagger = data ? data->LookupForm(0x1397E, "Skyrim.esm") : nullptr;
                auto* ruby = data ? data->LookupForm(0x63B42, "Skyrim.esm") : nullptr;
                if (!dagger || !ruby) return why = "no dagger or gem in the load order", false;
                a.propBase[0] = dagger->GetFormID();
                a.propName[0] = "Bloodied " + ItemNames::Display(NameOf(a.propBase[0]));
                a.propBase[1] = ruby->GetFormID();
                a.propName[1] = ShortName(NameOf(a.cast[A::RVictim])) + "'s " + ItemNames::Display(NameOf(a.propBase[1]));
                return true;
            }
            case A::Pattern::Feud: {
                const Folk* rival = pick(kBrawler, {me->home}, true, false);
                if (!rival) return why = "nobody in town to be at odds with", false;
                a.cast[A::RRival] = rival->ref;
                return true;
            }
            default:
                return why = "this kind of story is not ready yet", false;
        }
    }

    bool ArcBridge::AskedWithin(RE::FormID actor, float days) const {
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        for (const auto& a : m_book.all) {
            if (a.cast[A::RAsker] == actor && a.phase == A::Phase::Ended && a.ending != A::Ending::Lapsed && now - a.since < days) return true;
        }
        return false;
    }

    std::uint32_t ArcBridge::PlanFor(RE::Actor* asker, int pattern, bool hereOnly) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!asker || !player) return 0;
        const float now = NowDays();
        std::string why;
        if (hereOnly) {
            // A town's story comes to a player who is in that town (a stolen ring is no reason to cross Skyrim).
            const Folk* me = FolkOf(asker->GetFormID());
            auto* cell = player->GetParentCell();
            auto* here = TownOf(cell && cell->GetLocation() ? cell->GetLocation() : player->GetCurrentLocation());
            if (!me || !here || here->GetFormID() != me->town) {
                logger::info("Arcs: no arc for {}: the player is not in their town", asker->GetDisplayFullName());
                return 0;
            }
            // Someone who just asked (and was helped, refused or let down) does not come back with another story soon.
            if (AskedWithin(asker->GetFormID(), kAskerRestDays)) {
                logger::info("Arcs: no arc for {}: they asked the player for something lately", asker->GetDisplayFullName());
                return 0;
            }
        }
        if (!Usable(asker, kStays, why)) {
            logger::info("Arcs: no arc for {}: {}", asker->GetDisplayFullName(), why);
            return 0;
        }
        std::vector<A::Pattern> tries;
        A::Pattern last = A::Pattern::None;
        {
            std::scoped_lock lock(m_mutex);
            last = m_book.lastPattern;
        }
        if (pattern > 0 && pattern <= A::kPatterns) {
            tries.push_back(static_cast<A::Pattern>(pattern));
        } else {
            // Whichever fits, never the kind that ran last.
            for (const auto p : {A::Pattern::Thief, A::Pattern::Feud, A::Pattern::Murder}) {
                if (p != last) tries.push_back(p);
            }
        }
        for (const auto p : tries) {
            A::Arc a;
            a.pattern = p;
            a.seed = A::detail::Mix(asker->GetFormID(), static_cast<std::uint32_t>(now * 1440.0f) + static_cast<std::uint32_t>(p));
            if (!Cast(a, asker, why)) {
                logger::info("Arcs: no {} for {}: {}", static_cast<int>(p), asker->GetDisplayFullName(), why);
                continue;
            }
            if (p == A::Pattern::Murder) a.order = A::ShuffleSuspects(a.seed);
            a.gold = A::RewardGold(p, player->GetLevel());
            a.title = Fill(A::DefaultTitle(p), a);
            std::scoped_lock lock(m_mutex);
            // A pattern chosen on purpose (a test, the director's pick) is not refused for having run last.
            if (pattern > 0) m_book.lastPattern = A::Pattern::None;
            if (const auto refusal = m_book.Refusal(a); !refusal.empty()) {
                logger::info("Arcs: no {} for {}: {}", static_cast<int>(p), asker->GetDisplayFullName(), refusal);
                return 0;
            }
            const auto id = m_book.Plan(a, now);
            if (!id) return 0;
            m_pending.erase(id);
            std::string cast;
            for (std::size_t r = 1; r < A::kRoles; ++r) {
                if (a.cast[r]) cast += " " + std::to_string(r) + "=" + NameOf(a.cast[r]);
            }
            logger::info("Arcs: #{} planned: pattern {} in {},{}; prop '{}', {} gold", id, static_cast<int>(p), PlaceName(a.place), cast,
                         a.propName[0], a.gold);
            return id;
        }
        return 0;
    }

    bool ArcBridge::Accept(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        const bool ok = m_book.Accept(id, NowDays());
        logger::info("Arcs: #{} {}", id, ok ? "taken on" : "could not be taken on");
        if (const auto* a = ok ? m_book.Find(id) : nullptr) Spread(*a);
        return ok;
    }

    void ArcBridge::Spread(const A::Arc& a) {
        // A murder is known in the hold the moment it is open: everyone may speak of it (the witness and the suspects
        // know more, from their own lines).
        const std::string rumor = Fill(A::Rumor(a), a);
        if (!rumor.empty()) Tell(a.id, rumor, 0, a.cast[A::RAsker], a.place);
    }

    void ArcBridge::Tell(std::uint32_t id, const std::string& rumor, int replace, RE::FormID askerId, RE::FormID placeId) {
        auto* town = RE::TESForm::LookupByID<RE::BGSLocation>(placeId);
        auto* hold = HoldOf(town);
        const std::string holdName = hold && hold->GetFullName() ? hold->GetFullName() : "";
        const std::string askerName = NameOf(askerId);
        ++m_writing;
        std::thread([this, id, rumor, replace, holdName, askerName]() {
            if (replace > 0 && SkyrimNetAPI::RemoveWorldKnowledge) SkyrimNetAPI::RemoveWorldKnowledge(replace);
            int k = 0;
            if (!rumor.empty() && !holdName.empty() && SkyrimNetAPI::AddWorldKnowledge) {
                const std::string condition = "get_intelengine_hold(actorUUID) == \"" + Escape(holdName) + "\" and actorName != \"" + Escape(askerName) + "\"";
                const std::string label = std::string(kRumorLabel) + std::to_string(id);
                k = SkyrimNetAPI::AddWorldKnowledge(rumor.c_str(), condition.c_str(), false, 0.45f, label.c_str());
                logger::info("Arcs: #{} heard in the hold ({}): {}", id, k, rumor);
            }
            {
                std::scoped_lock lock(m_mutex);
                if (auto* arc = m_book.Find(id)) arc->rumor = k > 0 ? k : 0;
            }
            --m_writing;
        }).detach();
    }

    bool ArcBridge::Decline(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        if (!m_book.EndPlanned(id, A::Ending::Declined, NowDays())) return false;
        if (const auto* a = m_book.Find(id)) Ended(*a);
        return true;
    }

    bool ArcBridge::Drop(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        const bool ok = m_book.EndPlanned(id, A::Ending::Lapsed, NowDays());
        if (ok) logger::info("Arcs: #{} dropped before it was offered", id);
        return ok;
    }

    std::vector<std::uint32_t> ArcBridge::Lapse() {
        bool reconcile = false;
        std::vector<std::uint32_t> ids;
        {
            std::scoped_lock lock(m_mutex);
            ids = m_book.Lapse(NowDays());
            // A murder never brought to the player still happened: the town hears of it.
            for (const auto id : ids) {
                if (const auto* a = m_book.Find(id); a && a->pattern == A::Pattern::Murder) Ended(*a);
            }
            if (!m_reconciled && m_writing.load() == 0 && std::chrono::steady_clock::now() - m_loadedAt > std::chrono::seconds(20)) {
                m_reconciled = true;
                reconcile = true;
            }
        }
        if (reconcile) Reconcile();
        Pace();
        return ids;
    }

    void ArcBridge::Pace() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !m_catalogued) return;
        const float now = NowDays();
        auto* cell = player->GetParentCell();
        auto* town = TownOf(cell && cell->GetLocation() ? cell->GetLocation() : player->GetCurrentLocation());
        const RE::FormID townId = town ? town->GetFormID() : 0;
        if (townId != m_paceTown || now < m_paceSince) {
            m_paceTown = townId;
            m_paceSince = now;
            return;
        }
        if (!townId || (now - m_paceSince) * 24.0f < kTownLingerHours) return;
        if (m_paceTried >= 0.0f && now >= m_paceTried && now - m_paceTried < kPaceRetryDays) return;
        if (LivingWorldBridge::GetSingleton()->IsBadMoment() || player->IsInCombat()) return;
        {
            std::scoped_lock lock(m_mutex);
            if (m_book.Count(A::Phase::Planned) > 0 || m_book.Count(A::Phase::Open) >= A::kMaxOpen) return;
            float last = -1.0f;
            for (const auto& a : m_book.all) {
                if (a.ending != A::Ending::Lapsed) last = std::max(last, a.created);
            }
            if (last >= 0.0f && now >= last && now - last < kArcGapDays) return;
        }
        m_paceTried = now;
        // Who in this town might come: anyone with a home here, in an order of the hour.
        std::vector<const Folk*> folk;
        for (const auto& f : m_folk) {
            if (f.town == townId && f.home) folk.push_back(&f);
        }
        const auto seed = A::detail::Mix(townId, static_cast<std::uint32_t>(now * 24.0f));
        std::sort(folk.begin(), folk.end(), [seed](const Folk* x, const Folk* y) {
            return A::detail::Mix(seed, x->ref) < A::detail::Mix(seed, y->ref);
        });
        int tries = 0;
        const auto started = std::chrono::steady_clock::now();
        const auto spent = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
        for (const auto* f : folk) {
            if (++tries > 6) break;
            auto* asker = RE::TESForm::LookupByID<RE::Actor>(f->ref);
            if (!asker || asker->IsDead() || asker->IsDisabled()) continue;
            const auto id = PlanFor(asker, 0, true);
            if (!id) continue;
            logger::info("Arcs: #{} begins in {} after {:.1f} hours there: {} comes to the player ({:.2f} ms)", id, PlaceName(townId),
                         (now - m_paceSince) * 24.0f, NameOf(f->ref), spent());
            m_paceSince = now;
            if (auto* source = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent event{};
                event.eventName = "IntelEngine_ArcOffer";
                event.strArg = "";
                event.numArg = static_cast<float>(id);
                event.sender = asker;
                source->SendEvent(&event);
            }
            return;
        }
        logger::info("Arcs: nobody in {} has a story for the player now ({:.2f} ms)", PlaceName(townId), spent());
    }

    std::vector<std::uint32_t> ArcBridge::OpenIds() const {
        std::scoped_lock lock(m_mutex);
        return m_book.OpenIds();
    }

    // ------------------------------------------------------------------ the facts

    bool ArcBridge::IsHomeOf(RE::TESObjectCELL* cell, const A::Arc& a, A::Role role) const {
        if (!cell || !cell->IsInteriorCell() || !a.cast[role]) return false;
        if (const Folk* f = FolkOf(a.cast[role]); f && f->home == cell->GetFormID()) return true;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(a.cast[role]);
        if (!actor) return false;
        if (OwnedBy(cell->GetOwner(), actor)) return true;
        // A bed of theirs is here (the player is in the cell, so its references are loaded): where they sleep is home.
        bool bed = false;
        cell->ForEachReference([&](RE::TESObjectREFR* ref) {
            if (ref && !ref->IsDisabled() && OwnedBy(ref->GetOwner(), actor) && CellAnalyzer::GetSingleton()->IsBedFurniture(ref)) {
                bed = true;
                return RE::BSContainer::ForEachResult::kStop;
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return bed;
    }

    namespace {
        // Where a prop is hidden in someone's home (the player is in it, so its references are loaded): a container
        // of theirs that keeps what is put in it (not a respawning one), unlocked first; else their bed, to lie on.
        struct Spot {
            RE::TESObjectREFR* ref = nullptr;
            bool floor = false;
        };
        Spot HidingSpot(RE::TESObjectCELL* cell, RE::Actor* owner) {
            Spot best;
            int bestScore = -1;
            RE::TESObjectREFR* bed = nullptr;
            auto* cellOwner = cell->GetOwner();
            cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                if (!ref || ref->IsDisabled() || ref->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
                auto* base = ref->GetBaseObject();
                if (!base) return RE::BSContainer::ForEachResult::kContinue;
                auto* refOwner = ref->GetOwner();
                const bool theirs = OwnedBy(refOwner, owner) || (!refOwner && OwnedBy(cellOwner, owner));
                if (!theirs) return RE::BSContainer::ForEachResult::kContinue;
                if (auto* container = base->As<RE::TESObjectCONT>()) {
                    if (container->data.flags.any(RE::CONT_DATA::Flag::kRespawn)) return RE::BSContainer::ForEachResult::kContinue;
                    if (ref->extraList.HasType<RE::ExtraAliasInstanceArray>()) return RE::BSContainer::ForEachResult::kContinue;
                    int score = 1;
                    if (!ref->IsLocked()) score += 4;
                    if (OwnedBy(refOwner, owner)) score += 2;
                    if (score > bestScore) {
                        bestScore = score;
                        best.ref = ref;
                    }
                } else if (!bed && OwnedBy(refOwner, owner) && CellAnalyzer::GetSingleton()->IsBedFurniture(ref)) {
                    bed = ref;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            if (!best.ref && bed) {
                best.ref = bed;
                best.floor = true;
            }
            return best;
        }
    }

    namespace {
        // What the player may hand over on this node, and to whom: a Give node's prop to its person, or a Handed
        // branch's. False when nothing.
        bool HandOverOf(const A::Node& n, A::Role& to, std::uint8_t& prop) {
            if (n.kind == A::Kind::Give && n.who) {
                to = n.who;
                prop = n.prop;
                return true;
            }
            for (const auto& e : n.edges) {
                if (e.when == A::If::Handed && e.role) {
                    to = e.role;
                    prop = e.prop;
                    return true;
                }
            }
            return false;
        }
    }

    std::int32_t ArcBridge::Advance(std::uint32_t id) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return 0;
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || !A::IsOpen(*a)) return 0;
        auto& pending = m_pending[id];
        A::Facts f;
        std::array<RE::Actor*, A::kRoles> actors{};
        for (std::size_t r = 1; r < A::kRoles; ++r) {
            if (!a->cast[r]) continue;
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a->cast[r]);
            actors[r] = actor;
            f.dead[r] = !actor || actor->IsDead() || actor->IsDeleted();
            f.with[r] = actor && PlayerIsWith(actor, kWithDistance);
            f.spoke[r] = f.with[r] && SpokeWithPlayer(a->cast[r]);
        }
        for (std::size_t p = 0; p < A::kProps; ++p) {
            f.has[p] = a->propBase[p] && FindStamped(player, a->propBase[p], a->propName[p]).object != nullptr;
            f.handed[p] = (pending.handed & (1u << p)) != 0;
        }
        if (a->scene) {
            auto* here = player->GetParentCell();
            f.atScene = here && here->GetFormID() == a->scene;   // the victim's home
        }
        f.answer = pending.answer;
        f.brawl = pending.brawl;
        f.speech = static_cast<int>(player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech));
        // The prop is with the one it goes to, however it got there: the popup, or their own TakeItem action in talk
        // ("give it here": SeverActions moved Ysolda's ring out of the player's pack, and the hand-over never came,
        // 2026-11-18), or a gift through the game's own menus.
        if (a->node < A::Def(a->pattern).nodes.size()) {
            A::Role to = A::RNone;
            std::uint8_t prop = 0;
            if (HandOverOf(A::Def(a->pattern).nodes[a->node], to, prop) && !f.has[prop] && actors[to] &&
                FindStamped(actors[to], a->propBase[prop], a->propName[prop]).object) {
                f.handed[prop] = true;
            }
        }
        const auto change = A::Advance(*a, f, now);
        pending.answer = -1;
        std::int32_t bits = 0;
        if (change.any()) {
            for (const auto node : change.done) bits |= 1 << node;
            bits |= static_cast<std::int32_t>(change.ending) << 8;
            bits |= (static_cast<std::int32_t>(change.effect) & 0xF) << 16;
            bits |= (static_cast<std::int32_t>(change.effectRole) & 0xF) << 20;
            if (change.skipped != A::kNoNode) bits |= ((static_cast<std::int32_t>(change.skipped) + 1) & 0xF) << 27;
            if (change.stayed) bits |= 1 << 15;   // stayed on the step: a line to write, nothing done
            pending.log = change.log;
            pending.effect2 = static_cast<int>(change.effect2);
            pending.effect2Role = static_cast<int>(change.effect2Role);
            pending.handed = 0;
            pending.brawl = 0;
            // A new step asks afresh: the hold after an answer was the step's before (Ulfberth's peace popup waited two
            // minutes after Ysolda's decision, 2026-11-18).
            pending.chooseHold = -1.0f;
            pending.giveHold = -1.0f;
            // Still on the same step (refused): not asked again at once.
            if (change.stayed) pending.chooseHold = RealSeconds() + kHoldSeconds;
            logger::info("Arcs: #{} nodes done {:#x}, now at node {}{}{}", id, bits & 0xFF, static_cast<int>(a->node),
                         change.effect != A::Effect::None ? ", effect " + std::to_string(static_cast<int>(change.effect)) : std::string(),
                         change.ending != A::Ending::None ? ", ended " + std::to_string(static_cast<int>(change.ending)) : std::string());
            if (change.ending != A::Ending::None) {
                Ended(*a);
                return bits;
            }
        }
        // A prop hidden in the culprit's home goes there once the player is in it (its references are loaded only
        // then); nothing is put anywhere before the arc is taken on.
        const auto& def = A::Def(a->pattern);
        auto* cell = player->GetParentCell();
        const bool homeProp = a->pattern == A::Pattern::Thief || a->pattern == A::Pattern::Murder;
        // Whether this interior is the culprit's home is looked at once per cell the player enters (a scan of its beds).
        if (cell && cell->GetFormID() != pending.checkedCell) {
            pending.checkedCell = cell->GetFormID();
            pending.homeHere = homeProp && a->propBase[0] && !a->propAt[0] && IsHomeOf(cell, *a, A::RCulprit);
        }
        if (homeProp && a->propBase[0] && !a->propAt[0] && pending.plantProp < 0 && pending.homeHere) {
            const auto spot = HidingSpot(cell, actors[A::RCulprit]);
            if (spot.ref) {
                pending.plantAt = spot.ref->GetFormID();
                pending.plantProp = 0;
                pending.plantFloor = spot.floor;
                bits |= 1 << 24;
                logger::info("Arcs: #{} '{}' goes {} {:08X} in {}'s home", id, a->propName[0], spot.floor ? "beside" : "into",
                             spot.ref->GetFormID(), NameOf(a->cast[A::RCulprit]));
            }
        }
        if (a->node < def.nodes.size()) {
            const auto& n = def.nodes[a->node];
            const float real = RealSeconds();
            if ((n.kind == A::Kind::Choose || n.kind == A::Kind::Brawl) && n.who && f.spoke[n.who] && !f.dead[n.who] &&
                real >= pending.chooseHold) {
                pending.chooseHold = real + 30.0f;   // the popup is up; Answer sets the real hold
                bits |= 1 << 25;
            }
            A::Role to = A::RNone;
            std::uint8_t prop = 0;
            if (HandOverOf(n, to, prop) && f.has[prop] && f.spoke[to] && !f.dead[to] && real >= pending.giveHold && !(bits & (1 << 25))) {
                pending.giveHold = real + kHoldSeconds;
                bits |= 1 << 26;
            }
        }
        return bits;
    }

    bool ArcBridge::Answer(std::uint32_t id, int option) {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a || !A::IsOpen(*a)) return false;
        auto& pending = m_pending[id];
        if (option < 0) {
            pending.chooseHold = RealSeconds() + kHoldSeconds;
            pending.answer = -1;
            return true;
        }
        pending.answer = option;
        pending.chooseHold = RealSeconds() + kHoldSeconds;
        logger::info("Arcs: #{} answered {}", id, option);
        return true;
    }

    bool ArcBridge::HandOver(std::uint32_t id) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a || !A::IsOpen(*a) || !player) return false;
        const auto& def = A::Def(a->pattern);
        A::Role role = A::RNone;
        std::uint8_t prop = 0;
        if (a->node >= def.nodes.size() || !HandOverOf(def.nodes[a->node], role, prop)) return false;
        auto* to = RE::TESForm::LookupByID<RE::Actor>(a->cast[role]);
        if (!to || to->IsDead()) return false;
        const auto found = FindStamped(player, a->propBase[prop], a->propName[prop]);
        if (!found.object) {
            logger::info("Arcs: #{} hand-over refused: '{}' is not on the player", id, a->propName[prop]);
            return false;
        }
        player->RemoveItem(found.object, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, found.extra, to);
        m_pending[id].handed |= static_cast<std::uint8_t>(1u << prop);
        logger::info("Arcs: #{} '{}' handed to {}", id, a->propName[prop], to->GetDisplayFullName());
        return true;
    }

    void ArcBridge::GoldToPlayer(RE::FormID from, std::int32_t count) {
        if (!from || count <= 0) return;
        std::scoped_lock lock(m_mutex);
        for (auto& a : m_book.all) {
            if (!A::IsOpen(a) || a.cast[A::RAsker] != from) continue;
            a.paid = std::min<std::uint32_t>(a.paid + static_cast<std::uint32_t>(count), 1000000u);
            logger::info("Arcs: #{} {} gave the player {} gold ({} of {} paid)", a.id, NameOf(from), count, a.paid, a.gold);
        }
    }

    namespace {
        // Gold that reaches the player from someone who asked for help in an open arc, whoever moved it (SkyrimNet's or
        // SeverActions' GiveGold, a script): it counts toward the reward, so nobody pays twice. Carlotta paid 155 with
        // her own action when told "here is your ring", before it was handed over (2026-11-18). One compare per
        // inventory change otherwise.
        class GoldSink : public RE::BSTEventSink<RE::TESContainerChangedEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* e, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override {
                if (!e || e->baseObj != 0xF || e->newContainer != 0x14 || !e->oldContainer || e->oldContainer == 0x14 || e->itemCount <= 0) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                // Trade and pockets are not payment: gold for goods the player sold (an asker who keeps a shop), or
                // taken from their pockets.
                if (auto* ui = RE::UI::GetSingleton(); ui && (ui->IsMenuOpen(RE::BarterMenu::MENU_NAME) || ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME))) {
                    return RE::BSEventNotifyControl::kContinue;
                }
                ArcBridge::GetSingleton()->GoldToPlayer(e->oldContainer, e->itemCount);
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    void ArcBridge::Listen() {
        static GoldSink sink;
        static bool listening = false;
        if (listening) return;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESContainerChangedEvent>(&sink);
            listening = true;
        }
    }

    RE::Actor* ArcBridge::StageVictim(std::uint32_t id) {
        RE::Actor* victim = nullptr;
        {
            std::scoped_lock lock(m_mutex);
            const auto* a = m_book.Find(id);
            if (!a || a->pattern != A::Pattern::Murder || !A::IsLive(*a)) return nullptr;
            victim = RE::TESForm::LookupByID<RE::Actor>(a->cast[A::RVictim]);
        }
        // Never in the player's sight: taken home (where the game placed them) while out of every loaded cell.
        if (!victim || victim->IsDead() || victim->Is3DLoaded()) return nullptr;
        victim->MoveToEditorLocation();
        logger::info("Arcs: #{} {} is home, to be found there", id, NameOf(victim->GetFormID()));
        return victim;
    }

    bool ArcBridge::Brawl(std::uint32_t id, int result) {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a || !A::IsOpen(*a) || result == 0) return false;
        m_pending[id].brawl = result > 0 ? 1 : -1;
        logger::info("Arcs: #{} brawl {}", id, result > 0 ? "won" : "lost");
        return true;
    }

    void ArcBridge::Planted(std::uint32_t id, int prop, RE::TESObjectREFR* at) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        auto& pending = m_pending[id];
        pending.plantProp = -1;
        pending.plantAt = 0;
        if (!a || prop < 0 || prop >= static_cast<int>(A::kProps) || !at) return;
        a->propAt[static_cast<std::size_t>(prop)] = at->GetFormID();
        logger::info("Arcs: #{} '{}' is at {:08X}", id, a->propName[static_cast<std::size_t>(prop)], at->GetFormID());
    }

    // ------------------------------------------------------------------ fields for the script

    int ArcBridge::Field(std::uint32_t id, const std::string& name) const {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a) return -1;
        const auto& def = A::Def(a->pattern);
        const auto pending = m_pending.find(id);
        if (name == "phase") return static_cast<int>(a->phase);
        if (name == "pattern") return static_cast<int>(a->pattern);
        if (name == "slot") return a->slot == 0xFF ? -1 : a->slot;
        if (name == "node") return a->node;
        if (name == "nodes") return static_cast<int>(def.nodes.size());
        if (name == "gold") return static_cast<int>(a->gold);
        if (name == "paid") return static_cast<int>(a->paid);
        if (name == "ending") return static_cast<int>(a->ending);
        if (name == "openstage") return A::OpenStage(a->pattern);
        if (name == "endstage") return A::EndStage(a->pattern, a->ending);
        if (name == "log") return pending != m_pending.end() ? pending->second.log : 0;
        if (name == "effect2") return pending != m_pending.end() ? pending->second.effect2 : 0;
        if (name == "effect2role") return pending != m_pending.end() ? pending->second.effect2Role : 0;
        if (name == "plantprop") return pending != m_pending.end() ? pending->second.plantProp : -1;
        if (name == "plantfloor") return pending != m_pending.end() && pending->second.plantFloor ? 1 : 0;
        if (name == "kind") return a->node < def.nodes.size() && A::IsOpen(*a) ? static_cast<int>(def.nodes[a->node].kind) : 0;
        if (name == "objective") {
            return a->node < def.nodes.size() && A::IsOpen(*a) ? def.nodes[a->node].objective : -1;
        }
        if (name.starts_with("objective")) {
            const auto n = static_cast<std::size_t>(std::atoi(name.c_str() + 9));
            return n < def.nodes.size() ? def.nodes[n].objective : -1;
        }
        if (name == "options") {
            if (a->node >= def.nodes.size()) return 0;
            int count = 0;
            for (const auto* o : def.nodes[a->node].options) count += o ? 1 : 0;
            return count;
        }
        return -1;
    }

    RE::TESForm* ArcBridge::Form(std::uint32_t id, const std::string& name) const {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a) return nullptr;
        const auto& def = A::Def(a->pattern);
        const auto actor = [&](std::size_t r) -> RE::TESForm* { return r < A::kRoles && a->cast[r] ? RE::TESForm::LookupByID(a->cast[r]) : nullptr; };
        if (name == "asker") return actor(A::RAsker);
        if (name == "culprit") return actor(A::RCulprit);
        if (name == "witness") return actor(A::RWitness);
        if (name == "rival") return actor(A::RRival);
        if (name == "victim") return actor(A::RVictim);
        if (name == "suspect2") return actor(A::RSuspect2);
        if (name == "suspect3") return actor(A::RSuspect3);
        if (name.starts_with("role")) return actor(static_cast<std::size_t>(std::atoi(name.c_str() + 4)));
        if (name == "place") return a->place ? RE::TESForm::LookupByID(a->place) : nullptr;
        if (name.starts_with("prop")) {
            const auto p = static_cast<std::size_t>(std::atoi(name.c_str() + 4));
            return p < A::kProps && a->propBase[p] ? RE::TESForm::LookupByID(a->propBase[p]) : nullptr;
        }
        if (name == "plantat") {
            const auto pending = m_pending.find(id);
            return pending != m_pending.end() && pending->second.plantAt ? RE::TESForm::LookupByID(pending->second.plantAt) : nullptr;
        }
        if (name == "jail" || name == "jailout") {
            // The hold's jail (where the player is put) and where the hold lets prisoners out. Asked for when the story
            // has just ended too (a killer named with proof): "no jail for Nazeem" when it was open-only (2026-11-18).
            auto* culprit = RE::TESForm::LookupByID<RE::Actor>(a->cast[A::RCulprit]);
            auto* crime = culprit ? culprit->GetCrimeFaction() : nullptr;
            if (!crime) return nullptr;
            return name == "jail" ? crime->crimeData.factionJailMarker : crime->crimeData.factionWaitMarker;
        }
        if (a->node >= def.nodes.size() || !A::IsOpen(*a)) return nullptr;
        const auto& n = def.nodes[a->node];
        if (name == "who") return actor(n.who);
        if (name == "giveto") {
            A::Role to = A::RNone;
            std::uint8_t prop = 0;
            return HandOverOf(n, to, prop) ? actor(to) : nullptr;
        }
        if (name == "marker") {
            // Finding a prop: the marker goes to where it is hidden once it is there; else to the node's person.
            if (n.kind == A::Kind::Have && a->propAt[n.prop]) {
                auto* at = RE::TESForm::LookupByID<RE::TESObjectREFR>(a->propAt[n.prop]);
                if (at && !at->As<RE::Actor>()) return at;
            }
            return actor(n.marker);
        }
        return nullptr;
    }

    std::string ArcBridge::Fill(std::string text, const A::Arc& a) const {
        static constexpr std::pair<std::string_view, A::Role> roles[] = {
            {"{asker}", A::RAsker},       {"{culprit}", A::RCulprit},   {"{witness}", A::RWitness},  {"{rival}", A::RRival},
            {"{victim}", A::RVictim},     {"{suspect2}", A::RSuspect2}, {"{suspect3}", A::RSuspect3},
        };
        for (const auto& [key, role] : roles) {
            if (text.find(key) != std::string::npos) text = A::Fill(std::move(text), key, NameOf(a.cast[role]));
        }
        for (std::size_t i = 0; i < 3; ++i) {
            const std::string key = "{pick" + std::to_string(i) + "}";
            if (text.find(key) != std::string::npos) text = A::Fill(std::move(text), key, NameOf(a.cast[a.order[i]]));
        }
        for (std::size_t p = 0; p < A::kProps; ++p) {
            const std::string key = "{item" + std::to_string(p) + "}";
            if (text.find(key) != std::string::npos) text = A::Fill(std::move(text), key, a.propName[p]);
            const std::string thing = "{thing" + std::to_string(p) + "}";
            if (text.find(thing) != std::string::npos) text = A::Fill(std::move(text), thing, ItemNames::Display(NameOf(a.propBase[p])));
        }
        text = A::Fill(std::move(text), "{paid}", std::to_string(a.paid));
        if (text.find("{accused}") != std::string::npos) text = A::Fill(std::move(text), "{accused}", NameOf(a.cast[a.accused]));
        text = A::Fill(std::move(text), "{gold}", std::to_string(a.gold));
        text = A::Fill(std::move(text), "{place}", PlaceName(a.place));
        text = A::Fill(std::move(text), "{player}", PlayerName());
        return text;
    }

    std::string ArcBridge::Text(std::uint32_t id, const std::string& name) const {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a) return {};
        if (name == "narration") return Fill(A::OfferNarration(*a), *a);
        if (name == "prompt") return Fill(A::OfferPrompt(*a), *a);
        if (name == "title") return a->title;
        if (name == "place") return PlaceName(a->place);
        if (name.starts_with("prop")) {
            const auto p = static_cast<std::size_t>(std::atoi(name.c_str() + 4));
            return p < A::kProps ? a->propName[p] : std::string();
        }
        const auto& def = A::Def(a->pattern);
        if (a->node >= def.nodes.size()) return {};
        const auto& n = def.nodes[a->node];
        if (name == "who") return NameOf(a->cast[n.who]);
        if (name.starts_with("option")) {
            const auto i = static_cast<std::size_t>(std::atoi(name.c_str() + 6));
            if (i >= 3 || !n.options[i]) return {};
            // An answer that costs the player gold is not offered when they do not have it.
            for (const auto& e : n.edges) {
                const bool thisAnswer = e.when == static_cast<A::If>(static_cast<int>(A::If::Option0) + static_cast<int>(i));
                if (thisAnswer && e.effect == A::Effect::PlayerPays) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0xF);
                    if (!player || !gold || player->GetItemCount(gold) < static_cast<std::int32_t>(a->gold)) return {};
                }
            }
            return Fill(n.options[i], *a);
        }
        if (name == "fight") return Fill("Settle it with {rival} here and now, with your fists?", *a);
        if (name == "question") {
            switch (n.objective) {
                case A::Objective::Confront: return Fill("{culprit} stole {item0}. What will you do with {culprit}?", *a);
                case A::Objective::DecideFeud: return Fill("How will you settle it between {asker} and {rival}?", *a);
                case A::Objective::MakePeace: return Fill("How will you make peace with {rival}?", *a);
                case A::Objective::NameKiller: return Fill("Who killed {victim}?", *a);
                default: return "What will you do?";
            }
        }
        if (name == "ask") {
            switch (n.objective) {
                case A::Objective::DecideFeud: return Fill("Tell {asker} how you will settle it with {rival}?", *a);
                case A::Objective::NameKiller: return Fill("Name {victim}'s killer to {asker} now?", *a);
                default: return Fill("Decide now?", *a);
            }
        }
        if (name == "give" || name == "giveto") {
            A::Role to = A::RNone;
            std::uint8_t prop = 0;
            if (!HandOverOf(n, to, prop)) return {};
            if (name == "giveto") return NameOf(a->cast[to]);
            return Fill("Give {item" + std::to_string(prop) + "} back to " + NameOf(a->cast[to]) + "?", *a);
        }
        return {};
    }

    // ------------------------------------------------------------------ what people know

    bool ArcBridge::Involves(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        return m_book.LiveFor(actor) != nullptr;
    }

    std::uint32_t ArcBridge::ArcOf(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.LiveFor(actor);
        return a ? a->id : 0u;
    }

    std::string ArcBridge::LinesFor(RE::Actor* actor) const {
        if (!actor) return {};
        const auto id = actor->GetFormID();
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        std::string out;
        for (const auto& a : m_book.all) {
            const auto role = m_book.RoleOf(a, id);
            if (role == A::RNone) continue;
            std::string line;
            if (A::IsLive(a)) {
                line = A::RoleLines(a, role);
                if (!line.empty() && A::IsOpen(a) && role == A::RAsker) line += " (" + Ago(now - a.opened) + ")";
            } else if (a.ending != A::Ending::Lapsed && now - a.since < kMemoryDays) {
                line = A::EndMemory(a, role);
                if (!line.empty()) line += " (" + Ago(now - a.since) + ")";
            }
            if (!line.empty()) out += "- " + Fill(line, a) + "\n";
        }
        return out;
    }

    void ArcBridge::ApplyTitles() const {
        std::scoped_lock lock(m_mutex);
        for (const auto& a : m_book.all) {
            if (a.slot == 0xFF || a.title.empty()) continue;
            if (auto* quest = JournalQuest(a.slot)) quest->fullName = a.title.c_str();
        }
    }

    void ArcBridge::Ended(const A::Arc& a) {
        struct Note {
            RE::FormID who;
            std::string text;
        };
        std::vector<Note> notes;
        for (std::size_t r = 1; r < A::kRoles; ++r) {
            if (!a.cast[r]) continue;
            // The dead remember nothing (Heimskr, killed before the confrontation, was given "nobody came for me").
            auto* person = RE::TESForm::LookupByID<RE::Actor>(a.cast[r]);
            if (!person || person->IsDead()) continue;
            const auto memory = A::EndMemory(a, static_cast<A::Role>(r));
            if (!memory.empty()) notes.push_back({a.cast[r], Fill(memory, a)});
        }
        const std::string rumor = Fill(A::Rumor(a), a);
        const std::string place = PlaceName(a.place);
        const auto id = a.id;
        logger::info("Arcs: #{} ended ({}){}", id, static_cast<int>(a.ending), rumor.empty() ? std::string() : ": " + rumor);
        std::thread([notes, place, id]() {
            for (const auto& note : notes) {
                if (!SkyrimNetAPI::AddMemory) break;
                const int m = SkyrimNetAPI::AddMemory(note.who, note.text.c_str(), 0.7f, "EXPERIENCE", "", place.c_str(), "[\"intelengine_arc\"]", "[]");
                logger::info("Arcs: #{} memory {} for {:08X}: {}", id, m, note.who, note.text);
            }
        }).detach();
        // What the hold hears now replaces what it heard while the story was open (a murder's "found dead").
        if (!rumor.empty() || a.rumor > 0) Tell(id, rumor, a.rumor, a.cast[A::RAsker], a.place);
    }

    void ArcBridge::Reconcile() {
        // What the town heard of an arc this save's record does not have (it ended after this save was made, in a
        // timeline the player left by loading): withdrawn, or people tell of a story that did not happen here. Seen
        // 2026-11-18: after a reload, Saadia spoke of Carlotta's ring from the run before.
        if (!SkyrimNetAPI::GetWorldKnowledge || !SkyrimNetAPI::RemoveWorldKnowledge) return;
        std::unordered_set<int> known;
        {
            std::scoped_lock lock(m_mutex);
            for (const auto& a : m_book.all) {
                if (a.rumor > 0) known.insert(a.rumor);
            }
        }
        std::thread([known]() {
            const std::string json = SkyrimNetAPI::GetWorldKnowledge(100000);
            auto entries = nlohmann::json::parse(json, nullptr, false);
            if (!entries.is_array()) return;
            int withdrawn = 0;
            for (const auto& entry : entries) {
                const auto label = entry.value("display_name", std::string());
                const int id = entry.value("id", 0);
                if (id <= 0 || !label.starts_with(kRumorLabel) || known.contains(id)) continue;
                if (SkyrimNetAPI::RemoveWorldKnowledge(id)) ++withdrawn;
            }
            logger::info("Arcs: {} rumor(s) of other timelines withdrawn, {} kept", withdrawn, known.size());
        }).detach();
    }

    // ------------------------------------------------------------------ save data

    void ArcBridge::Save(SKSE::SerializationInterface* intfc) {
        std::vector<std::uint8_t> bytes;
        {
            std::scoped_lock lock(m_mutex);
            bytes = A::Serialize(m_book);
        }
        if (!intfc->OpenRecord(A::kRecordType, A::kRecordVersion)) {
            logger::error("Arcs: could not open the co-save record");
            return;
        }
        intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }

    void ArcBridge::Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version != A::kRecordVersion || length > (1u << 20)) {
            logger::warn("Arcs: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("Arcs: the co-save record is short; starting without arcs");
            return;
        }
        A::Book book;
        if (!A::Deserialize(bytes, book, version)) {
            logger::warn("Arcs: the co-save record is damaged; starting without arcs");
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
        std::erase_if(book.all, [&](A::Arc& a) {
            if (!resolve(a.cast[A::RAsker]) || a.cast[A::RAsker] == 0) return true;   // the asker is gone from the load order
            for (std::size_t r = 2; r < A::kRoles; ++r) resolve(a.cast[r]);
            for (std::size_t p = 0; p < A::kProps; ++p) {
                resolve(a.propBase[p]);
                resolve(a.propAt[p]);
            }
            resolve(a.place);
            resolve(a.scene);
            return false;
        });
        std::scoped_lock lock(m_mutex);
        m_book = std::move(book);
        m_pending.clear();
        m_reconciled = false;
        m_loadedAt = std::chrono::steady_clock::now();
        logger::info("Arcs: loaded {} ({} open)", m_book.all.size(), m_book.Count(A::Phase::Open));
    }

    void ArcBridge::Revert() {
        std::scoped_lock lock(m_mutex);
        m_book = A::Book{};
        m_pending.clear();
        // A save without arcs (made before them, or before this mod) still gets its look: every arc rumor is of another
        // timeline then.
        m_reconciled = false;
        m_loadedAt = std::chrono::steady_clock::now();
    }

    // ------------------------------------------------------------------ natives (script IntelEngine)

    namespace {
        std::vector<std::int32_t> ToInts(const std::vector<std::uint32_t>& ids) { return std::vector<std::int32_t>(ids.begin(), ids.end()); }
        std::uint32_t U(std::int32_t id) { return static_cast<std::uint32_t>(id); }
        // Field names come through the game's case-insensitive string pool ("Marker" if any script spelled it so
        // first): compared in lower case.
        std::string S(const RE::BSFixedString& s) {
            std::string out = s.c_str() ? s.c_str() : "";
            for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        std::int32_t ArcPlan(RE::StaticFunctionTag*, RE::Actor* asker, std::int32_t pattern) {
            return static_cast<std::int32_t>(ArcBridge::GetSingleton()->PlanFor(asker, pattern));
        }
        bool ArcAccept(RE::StaticFunctionTag*, std::int32_t id) {
            const bool ok = ArcBridge::GetSingleton()->Accept(U(id));
            if (ok) ArcBridge::GetSingleton()->ApplyTitles();
            return ok;
        }
        bool ArcDecline(RE::StaticFunctionTag*, std::int32_t id) { return ArcBridge::GetSingleton()->Decline(U(id)); }
        bool ArcDrop(RE::StaticFunctionTag*, std::int32_t id) { return ArcBridge::GetSingleton()->Drop(U(id)); }
        std::vector<std::int32_t> ArcLapse(RE::StaticFunctionTag*) { return ToInts(ArcBridge::GetSingleton()->Lapse()); }
        std::vector<std::int32_t> ArcOpenIds(RE::StaticFunctionTag*) { return ToInts(ArcBridge::GetSingleton()->OpenIds()); }
        std::int32_t ArcAdvance(RE::StaticFunctionTag*, std::int32_t id) { return ArcBridge::GetSingleton()->Advance(U(id)); }
        bool ArcAnswer(RE::StaticFunctionTag*, std::int32_t id, std::int32_t option) { return ArcBridge::GetSingleton()->Answer(U(id), option); }
        bool ArcHandOver(RE::StaticFunctionTag*, std::int32_t id) { return ArcBridge::GetSingleton()->HandOver(U(id)); }
        bool ArcBrawl(RE::StaticFunctionTag*, std::int32_t id, std::int32_t result) { return ArcBridge::GetSingleton()->Brawl(U(id), result); }
        RE::Actor* ArcStageVictim(RE::StaticFunctionTag*, std::int32_t id) { return ArcBridge::GetSingleton()->StageVictim(U(id)); }
        void ArcPlanted(RE::StaticFunctionTag*, std::int32_t id, std::int32_t prop, RE::TESObjectREFR* at) {
            ArcBridge::GetSingleton()->Planted(U(id), prop, at);
        }
        std::int32_t ArcField(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) { return ArcBridge::GetSingleton()->Field(U(id), S(name)); }
        RE::TESForm* ArcForm(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) { return ArcBridge::GetSingleton()->Form(U(id), S(name)); }
        RE::BSFixedString ArcText(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) {
            return RE::BSFixedString(ArcBridge::GetSingleton()->Text(U(id), S(name)).c_str());
        }
        void ArcApplyTitles(RE::StaticFunctionTag*) { ArcBridge::GetSingleton()->ApplyTitles(); }
    }

    void ArcBridge::Register(RE::BSScript::IVirtualMachine* a_vm, int& count) {
        constexpr auto script = "IntelEngine";
        a_vm->RegisterFunction("ArcPlan", script, ArcPlan); ++count;
        a_vm->RegisterFunction("ArcAccept", script, ArcAccept); ++count;
        a_vm->RegisterFunction("ArcDecline", script, ArcDecline); ++count;
        a_vm->RegisterFunction("ArcDrop", script, ArcDrop); ++count;
        a_vm->RegisterFunction("ArcLapse", script, ArcLapse); ++count;
        a_vm->RegisterFunction("ArcOpenIds", script, ArcOpenIds); ++count;
        a_vm->RegisterFunction("ArcAdvance", script, ArcAdvance); ++count;
        a_vm->RegisterFunction("ArcAnswer", script, ArcAnswer); ++count;
        a_vm->RegisterFunction("ArcHandOver", script, ArcHandOver); ++count;
        a_vm->RegisterFunction("ArcBrawl", script, ArcBrawl); ++count;
        a_vm->RegisterFunction("ArcStageVictim", script, ArcStageVictim); ++count;
        a_vm->RegisterFunction("ArcPlanted", script, ArcPlanted); ++count;
        a_vm->RegisterFunction("ArcField", script, ArcField); ++count;
        a_vm->RegisterFunction("ArcForm", script, ArcForm); ++count;
        a_vm->RegisterFunction("ArcText", script, ArcText); ++count;
        a_vm->RegisterFunction("ArcApplyTitles", script, ArcApplyTitles); ++count;
    }
}
