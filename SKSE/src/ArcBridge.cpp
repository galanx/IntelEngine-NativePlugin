#include "ArcBridge.h"
#include "FrameCost.h"
#include "RumorBridge.h"
#include "Safe.h"
#include "HeartBridge.h"

#include "CellAnalyzer.h"
#include "ItemNames.h"
#include "LivingWorldBridge.h"
#include "LocationResolver.h"
#include "NPCIndex.h"
#include "MemoryDB.h"
#include "DialogueTracker.h"
#include "StoryDirectorPick.h"
#include "QuestBridge.h"
#include "Papyrus.h"
#include "SkyrimNetAPI.h"
#include "SpeechTracker.h"
#include "Tales.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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

        // Out in the wilds: outdoors, and nowhere people live (a town, a farm, an inn, a home). An ambush falls there
        // only, never where guards or townsfolk would join in.
        bool InTheWilds(RE::TESObjectREFR* ref) {
            auto* cell = ref ? ref->GetParentCell() : nullptr;
            if (!cell || cell->IsInteriorCell()) return false;
            int depth = 0;
            for (auto* loc = ref->GetCurrentLocation(); loc && depth < 12; loc = loc->parentLoc, ++depth) {
                for (const char* kw : {"LocTypeCity", "LocTypeTown", "LocTypeSettlement", "LocTypeHabitation", "LocTypeDwelling",
                                       "LocTypeInn", "LocTypePlayerHouse", "LocTypeFarm", "LocTypeCastle", "LocTypeJail", "LocTypeStore"}) {
                    if (loc->HasKeywordString(kw)) return false;
                }
            }
            return true;
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

        std::atomic<RE::FormID> g_forcedCast{0};

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
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> favours;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> places;
        std::size_t aliasCount = 0;
        if (auto* data = RE::TESDataHandler::GetSingleton()) {
            // The game's own small quests of a person (a favour with a name of its own: the letter between Faendal,
            // Sven and Camilla): they keep nobody out of a story, but the author is told of them so that no tale
            // retells one (the user, 2026-11-19: "never give the same letter via vanilla quest and then dungeon
            // master quest to camilla"). Only those that name a few people; the ones that name half a town say nothing.
            for (auto* quest : data->GetFormArray<RE::TESQuest>()) {
                if (!quest || quest->GetType() != RE::QUEST_DATA::Type::kMiscellaneous) continue;
                const char* title = quest->GetFullName();
                if (!title || !*title) continue;
                std::vector<RE::FormID> people;
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
                    if (npc && std::find(people.begin(), people.end(), npc->GetFormID()) == people.end()) people.push_back(npc->GetFormID());
                }
                if (people.empty() || people.size() > 6) continue;
                for (const auto who : people) favours[who].push_back(quest->GetFormID());
            }
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
                    // Where a quest of the game put one of its own references (the Golden Claw in Bleak Falls
                    // Barrow, a quest's giver in their hall): that place, and the places it lies in, belong to it.
                    if (fill == RE::BGSBaseAlias::FILL_TYPE::kForced) {
                        auto forced = refAlias->fillData.forced.forcedRef.get();
                        auto* where = forced ? forced->GetCurrentLocation() : nullptr;
                        int depth = 0;
                        for (auto* loc = where; loc && depth < 4; loc = loc->parentLoc, ++depth) {
                            if (loc->HasKeywordString("LocTypeHold") || loc->HasKeywordString("LocTypeCity") || loc->HasKeywordString("LocTypeTown")) break;
                            auto& quests = places[loc->GetFormID()];
                            if (std::find(quests.begin(), quests.end(), quest->GetFormID()) == quests.end()) quests.push_back(quest->GetFormID());
                        }
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
        logger::info("Arcs: {} townsfolk with a home ({}), {} actors named by {} aliases of journal quests, {} places quests put something in, {:.1f} ms",
                     folk.size(), towns, named.size(), aliasCount, places.size(), ms);
        std::scoped_lock lock(m_mutex);
        m_folk = std::move(folk);
        m_journalAliases = std::move(named);
        m_favours = std::move(favours);
        m_questPlaces = std::move(places);
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
        // Test hook (ArcDebugCast): the next arc's thief, rival or killer is this person when they may be cast at all.
        RE::FormID forcedFirst = g_forcedCast.exchange(0);
        const auto pick = [&](int need, std::initializer_list<RE::FormID> notHomes, bool byStanding, bool needsHome) -> const Folk* {
            const Folk* best = nullptr;
            int bestScore = -1000;
            if (forcedFirst) {
                const auto chosen = forcedFirst;
                forcedFirst = 0;
                for (const auto* f : others) {
                    if (f->ref != chosen) continue;
                    std::string reason;
                    if (Usable(RE::TESForm::LookupByID<RE::Actor>(f->ref), need, reason)) return f;
                    logger::info("Arcs: the chosen {} cannot be cast: {}", NameOf(f->ref), reason);
                }
            }
            std::string reasons;
            for (const auto* f : others) {
                if (f->home && std::find(notHomes.begin(), notHomes.end(), f->home) != notHomes.end()) continue;
                if (needsHome && !f->home) continue;
                if (need == kKillable && !f->placedHome) continue;   // found dead at home: the game must know it as theirs
                bool cast = false;
                for (const auto who : a.cast) cast = cast || who == f->ref;
                if (cast) continue;
                // People the game gives relationships to (family, friends, rivals) are its real townsfolk; a
                // placeholder such as the temple's "Sickly Farmer" (a patient in bed who answers nobody) has none.
                auto* fNpc = RE::TESForm::LookupByID<RE::TESNPC>(f->base);
                const int known = fNpc && fNpc->relationships && !fNpc->relationships->empty() ? 4 : 0;
                const int score = (byStanding ? standing(f) : 0) + known;
                if (score <= -90 || score <= bestScore) {
                    if (score <= -90) reasons += NameOf(f->ref) + " (close to " + NameOf(me->ref) + "), ";
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

    namespace {
        // "Stands with the player" means: hears what is said to the player. That is SkyrimNet's own measure, its
        // interaction distance (1600 units unless the user changed it; the script tells us, ArcEarshot): whoever is
        // within it hears a line and may answer it. In the test game of 2026-11-19 Alvor, 900 units off at his forge,
        // answered Faendal's talk about his arrowheads with a full confession before the player had asked him
        // anything, and Sven sang 500 units away in the same hall while Faendal would have spoken of him.
        constexpr float kEarshotDefault = 1600.0f;
        std::atomic<float> g_earshot{kEarshotDefault};

        // Someone of the story other than its asker who stands beside the player (the caller holds the lock, or owns
        // the arc). A follower of the player is not counted: they are always there, and the story would never come.
        RE::Actor* StandsBy(const A::Arc& a) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!cell) return nullptr;
            for (std::size_t role = 0; role < a.cast.size(); ++role) {
                if (role == static_cast<std::size_t>(A::RAsker) || !a.cast[role]) continue;
                auto* who = RE::TESForm::LookupByID<RE::Actor>(a.cast[role]);
                if (!who || who->IsDead() || !who->Is3DLoaded() || who->IsPlayerTeammate()) continue;
                auto* theirs = who->GetParentCell();
                if (!theirs || theirs->IsInteriorCell() != cell->IsInteriorCell() || (cell->IsInteriorCell() && theirs != cell)) continue;
                if (who->GetPosition().GetDistance(player->GetPosition()) <= g_earshot.load()) return who;
            }
            return nullptr;
        }
    }

    namespace {
        // The place (index + 1) a tale's step is about: where to go, what to kill there, or what its master carries.
        std::uint8_t SiteOfStep(const A::Arc& a, const A::Node& n) {
            if ((n.kind == A::Kind::Delve || n.kind == A::Kind::Slay) && n.site) return n.site;
            if (n.kind == A::Kind::Free && a.captiveSite) return a.captiveSite;
            if (n.kind == A::Kind::Have && n.prop < A::kProps && a.propSite[n.prop]) return a.propSite[n.prop];
            return 0;
        }

        // The ground outside a place: its marker on the map, which the game's own makers put by its way in. What a
        // story leaves to be found outside a place lies by it.
        RE::TESObjectREFR* OutsideOf(const A::Arc& a, std::uint8_t site) {
            if (!site || site > A::kSites) return nullptr;
            auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(a.site[site - 1u]);
            return location ? std::as_const(location->worldLocMarker).get().get() : nullptr;
        }

        // Where the journal points for such a step: the master (alive, or the body that still carries what is to be
        // found), else the heart of the place. The heart of many places (every Dwemer ruin looked at, 2026-11-19:
        // Mzinchaleft, Nchuand-Zel, Kagrenzel) is no reference of the game until its part of the place is loaded:
        // until then the way leads to the place itself, by its marker on the map, and once the player is inside
        // the journal's own words lead on (a marker out of doors would point back at the way in).
        RE::TESForm* PlaceMarker(const A::Arc& a, const A::Node& n, std::uint8_t site) {
            const auto s = static_cast<std::size_t>(site - 1u);
            auto* boss = a.siteBoss[s] ? RE::TESForm::LookupByID<RE::Actor>(a.siteBoss[s]) : nullptr;
            // Someone held there: the marker is on them once they are at the heart of the place; until then it
            // leads to the place like any other step out there, never to its master.
            if (n.kind == A::Kind::Free) {
                auto* held = a.captive ? RE::TESForm::LookupByID<RE::Actor>(a.cast[a.captive]) : nullptr;
                if (held && a.captiveState >= A::CaptiveBound && !held->IsDisabled() && !held->IsDead()) return held;
                boss = nullptr;
            }
            // What is to be found is on the master, alive or dead: the marker is on the body, wherever the fight
            // ended (played at White River Watch: the chief fell five hundred units from his room, and the marker
            // for "take the stone from him" pointed at the empty room). Held by the marker alias, the body also
            // stays in the world until it has been searched.
            if (boss && n.kind == A::Kind::Have && !boss->IsDeleted() && !boss->IsDisabled()) return boss;
            if (boss && !boss->IsDead() && n.kind != A::Kind::Delve) return boss;
            if (auto* heart = a.siteAnchor[s] ? RE::TESForm::LookupByID(a.siteAnchor[s]) : nullptr) return heart;
            auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(a.site[s]);
            auto* player = RE::PlayerCharacter::GetSingleton();
            for (auto* at = player ? player->GetCurrentLocation() : nullptr; at; at = at->parentLoc) {
                if (at == location) return nullptr;
            }
            auto onTheMap = location ? std::as_const(location->worldLocMarker).get() : RE::NiPointer<RE::TESObjectREFR>();
            return onTheMap.get();
        }

        // In the game right now: its cell is attached. (A marker has no 3D of its own, so "3D loaded" misses some.)
        bool InTheWorldNow(RE::TESObjectREFR* ref) {
            auto* cell = ref ? ref->GetParentCell() : nullptr;
            return cell && cell->IsAttached();
        }

        // Something the journal can point at: a reference of the game that is not switched off and not gone.
        bool Points(RE::TESObjectREFR* ref) { return ref && !ref->IsDeleted() && !ref->IsDisabled(); }

        // A door of an interior that leads out of it: the one to the street where there is one, else any (a room
        // above an inn's hall). Such doors are persistent, so it is found while the cell is not loaded, and the game
        // shows the way to it from outside.
        RE::TESObjectREFR* DoorOf(RE::TESObjectCELL* cell) {
            RE::TESObjectREFR* any = nullptr;
            RE::TESObjectREFR* street = nullptr;
            cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                if (!ref || ref->IsDisabled() || ref->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
                auto* base = ref->GetBaseObject();
                auto* teleport = base && base->Is(RE::FormType::Door) ? ref->extraList.GetByType<RE::ExtraTeleport>() : nullptr;
                if (!teleport || !teleport->teleportData) return RE::BSContainer::ForEachResult::kContinue;
                if (!any) any = ref;
                auto other = std::as_const(teleport->teleportData->linkedDoor).get();
                auto* beyond = other ? other->GetSaveParentCell() : nullptr;
                if (other && !beyond) beyond = other->GetParentCell();
                if (beyond && !beyond->IsInteriorCell()) {
                    street = ref;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return street ? street : any;
        }

        // What a look at a story costs the frame it runs in (the scripts call Advance every two seconds for each
        // open story). Under the bridge's lock.
        FrameStats g_lookCost;

        // The door of each home asked for so far (home cell -> door, 0 = it has none): a home's doors do not change,
        // and finding one means walking the cell's references. Read and written under the bridge's lock.
        std::unordered_map<RE::FormID, RE::FormID> g_homeDoors;
    }

    RE::TESObjectREFR* ArcBridge::HomeDoorOf(RE::FormID actorRef) const {
        const Folk* folk = actorRef ? FolkOf(actorRef) : nullptr;
        if (!folk || !folk->home) return nullptr;
        if (const auto known = g_homeDoors.find(folk->home); known != g_homeDoors.end()) {
            auto* door = known->second ? RE::TESForm::LookupByID<RE::TESObjectREFR>(known->second) : nullptr;
            return Points(door) ? door : nullptr;
        }
        auto* cell = RE::TESForm::LookupByID<RE::TESObjectCELL>(folk->home);
        const auto began = FrameClock::Now();
        auto* door = cell ? DoorOf(cell) : nullptr;
        g_homeDoors[folk->home] = door ? door->GetFormID() : 0u;
        // Once for each home, inside a frame: said, so a home with a great many things in it shows.
        logger::info("Arcs: the door of home {:08X}: {:08X} ({:.2f} ms of the game's frame, once)", folk->home, door ? door->GetFormID() : 0u,
                     std::chrono::duration<double, std::milli>(FrameClock::Now() - began).count());
        return Points(door) ? door : nullptr;
    }

    // Where the journal points, for every kind of step, as the game stands at this moment. A player does not do things
    // in the order a story was written in, and people do not stay where a marker was once put: so this is asked on
    // every watch (Advance), and the script is told whenever the answer has changed (the user, 2026-11-19: "Based on
    // player unpredictability, we may need a more reliable way to resolve the quest markers"). The order:
    //   what the step is about, where it is right now (a body that carries the thing, the place's master, the one
    //   held, the spot something was hidden at, the person to speak with);
    //   else where that is to be looked for (the home's door for something hidden there or for "go to their home",
    //   the heart of a place, the place on the map);
    //   else, for a person who is not in the game just now (sent away, taken, jailed out of sight), their home's door.
    // Never a reference that is switched off or gone: such a marker shows nothing, and the journal reads as broken.
    RE::TESForm* ArcBridge::MarkerOf(const A::Arc& a, const A::Node& n) const {
        const bool tale = a.pattern == A::Pattern::Tale;
        const auto refOf = [](RE::FormID id) { return id ? RE::TESForm::LookupByID<RE::TESObjectREFR>(id) : nullptr; };
        if (tale) {
            // What is to be looked at closely: on it once it lies there; until then the place on the map it lies by.
            if (n.kind == A::Kind::Examine && n.prop < A::kClues) {
                if (auto* lies = refOf(a.clueRef[n.prop]); a.clueState[n.prop] != A::ClueToPlace && Points(lies)) return lies;
                return OutsideOf(a, n.site);
            }
            // What walks by night: on it while it is out; by day the ground it walks.
            if (n.kind == A::Kind::Haunt) {
                if (auto* shade = refOf(a.spiritRef); a.spiritState == A::SpiritOut && Points(shade)) return shade;
                return OutsideOf(a, n.site);
            }
            // What the leader of an ambush carries: on the leader, standing or fallen.
            if (n.kind == A::Kind::Have && n.prop < A::kProps && (a.propAmbush & (1u << n.prop)) && a.thug[0]) {
                if (auto* leader = refOf(a.thug[0]); Points(leader)) return leader;
            }
            // A place out in the world: its master, the one held there, its heart, or the place on the map.
            if (const auto site = SiteOfStep(a, n)) return PlaceMarker(a, n, site);
            if (n.kind == A::Kind::Have && n.prop < A::kProps) {
                // Hidden somewhere and put there already: the spot itself.
                if (auto* at = refOf(a.propAt[n.prop]); at && !at->As<RE::Actor>() && Points(at)) return at;
                // Hidden in someone's home and not put there yet (it goes in when the player walks in): the home, by
                // its door. The marker used to sit on the owner, wherever in town they were, while the journal said
                // to search their house.
                if (!a.propAt[n.prop] && a.propHome[n.prop]) {
                    if (auto* door = HomeDoorOf(a.cast[a.propHome[n.prop]])) return door;
                }
            }
            // "Go to their home" is the home, not the person, who may be at the market.
            if (n.kind == A::Kind::Reach && n.who) {
                if (auto* door = HomeDoorOf(a.cast[n.who])) return door;
            }
        } else if (n.kind == A::Kind::Have && n.prop < A::kProps && a.propAt[n.prop]) {
            if (auto* at = refOf(a.propAt[n.prop]); at && !at->As<RE::Actor>() && Points(at)) return at;
        }
        const RE::FormID person = n.marker != A::RNone ? a.cast[n.marker] : 0u;
        if (auto* who = refOf(person); Points(who)) return who;
        // Not in the game just now: where they live, so the journal is not left blank.
        return person ? HomeDoorOf(person) : nullptr;
    }

    RE::Actor* ArcBridge::Company(std::uint32_t id) const {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        // The player came to the asker and spoke: what they are told then is theirs to hear, whoever stands near.
        if (const auto it = m_pending.find(id); it != m_pending.end() && it->second.sought) return nullptr;
        return a ? StandsBy(*a) : nullptr;
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
        // A tale is being written for this town: a story planned now would take its people from under it (the
        // director planned a theft with Arcadia while Heimskr's tale, with her in it, was being written, 2026-11-19).
        if (m_taleBusy.load() && pattern == 0) {
            logger::info("Arcs: no arc for {}: a tale is being written", asker->GetDisplayFullName());
            return 0;
        }
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
        {
            std::scoped_lock lock(m_mutex);
            if (const float last = m_book.LastBeat(), now = NowDays(); !A::QuietFor(last, now, A::kQuietHours)) {
                logger::info("Arcs: no arc for {}: a story came to the player or ended {:.1f} hours ago (quiet for {:.0f})",
                             asker->GetDisplayFullName(), (now - last) * 24.0f, A::kQuietHours);
                return 0;
            }
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
        // Taken on: the talk of someone looking for help is over (a story that began as talk in town).
        if (ok) RumorBridge::GetSingleton()->Withdraw(Rumors::Owner::Arc, id, Rumors::Kind::Trouble);
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
        // What the hold keeps (below) is also the talk of the day: brought up at the inn, carried to the next hold,
        // forgotten in a week or two (RumorBridge). The newer word of a story replaces its older one.
        RumorBridge::GetSingleton()->Withdraw(Rumors::Owner::Arc, id, Rumors::Kind::Deed);
        if (!rumor.empty()) RumorBridge::GetSingleton()->Note(Rumors::Kind::Deed, rumor, placeId, 0, 0, Rumors::Owner::Arc, id, 0.6f, false);
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
        RumorBridge::GetSingleton()->Withdraw(Rumors::Owner::Arc, id, Rumors::Kind::Trouble);
        if (const auto* a = m_book.Find(id)) Ended(*a);
        return true;
    }

    bool ArcBridge::Drop(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        const bool ok = m_book.EndPlanned(id, A::Ending::Lapsed, NowDays());
        if (ok) logger::info("Arcs: #{} dropped before it was offered", id);
        if (ok) RumorBridge::GetSingleton()->Withdraw(Rumors::Owner::Arc, id, Rumors::Kind::Trouble);
        return ok;
    }

    namespace {
        // Who is held bound right now: the one held of each open story while they are in their bonds (0 = nobody).
        // Read without a lock by the crosshair's text (NpcActivateText, further down), which the game asks for on its
        // own thread of play whenever the player looks at someone; written under the bridge's lock.
        std::array<std::atomic<RE::FormID>, 4> g_bound{};
        // And what lies outside a place to be looked at closely and has not been yet (the same reader).
        std::array<std::atomic<RE::FormID>, 4> g_clues{};
        // What walks by night and is out right now, for each story that has one; `rest`: the player carries what lets
        // it rest, so their hand on it lays it to rest (the same reader changes the crosshair's word).
        struct SpiritSlot {
            std::atomic<std::uint32_t> arc{0};
            std::atomic<RE::FormID> ref{0};
            std::atomic<bool> rest{false};
        };
        std::array<SpiritSlot, 4> g_spirits{};

        // Written under the bridge's lock.
        void SetSpirit(std::uint32_t arc, RE::FormID ref, bool rest) {
            SpiritSlot* slot = nullptr;
            for (auto& s : g_spirits) if (s.arc.load() == arc) slot = &s;
            if (!slot && ref) {
                for (auto& s : g_spirits) {
                    if (!s.arc.load()) {
                        slot = &s;
                        break;
                    }
                }
            }
            if (!slot) return;
            slot->rest.store(ref != 0 && rest);
            slot->ref.store(ref);
            slot->arc.store(ref ? arc : 0u);
        }

        // The game's own shades (Skyrim.esm): a woman of Yngvild's dead, a man of the cairns' dead. Leveled, as the
        // game made them; the script takes them out of their factions and their anger before anyone sees them.
        constexpr RE::FormID kShadeWoman = 0x00074360, kShadeMan = 0x000B1F96;

        float HourNow() {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetHour() : 12.0f;
        }

        void RefreshBound(const A::Book& book) {
            std::size_t k = 0, c = 0;
            for (const auto& a : book.all) {
                if (!A::IsOpen(a)) continue;
                if (k < g_bound.size() && a.captive && a.captiveState == A::CaptiveBound) g_bound[k++].store(a.cast[a.captive]);
                for (std::size_t i = 0; i < A::kClues; ++i) {
                    if (c < g_clues.size() && a.clueWhat[i] && a.clueState[i] == A::CluePlaced && a.clueRef[i]) g_clues[c++].store(a.clueRef[i]);
                }
            }
            for (; k < g_bound.size(); ++k) g_bound[k].store(0);
            for (; c < g_clues.size(); ++c) g_clues[c].store(0);
        }

        // The step of a tale that looks at clue `k`, if the story has not been past it yet: only then is anything
        // laid out there for it.
        bool ClueWanted(const A::Arc& a, std::size_t k) {
            const auto nodes = A::NodesOf(a);
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].kind == A::Kind::Examine && nodes[i].prop == k) return !(a.visited & (1u << i));
            }
            return false;
        }

        // What walks by night walks while the story has not been past it: after that the ground is quiet.
        bool SpiritWanted(const A::Arc& a) {
            if (!A::IsOpen(a) || !a.spiritSite || a.spiritState >= A::SpiritBanished) return false;
            const auto nodes = A::NodesOf(a);
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                if (nodes[i].kind == A::Kind::Haunt) return !(a.visited & (1u << i));
            }
            return false;
        }
    }

    std::vector<std::uint32_t> ArcBridge::Lapse() {
        bool reconcile = false;
        std::vector<std::uint32_t> ids;
        // The talk of the day moves on (once a game hour; this look runs every two seconds anyway).
        RumorBridge::GetSingleton()->Tick();
        {
            std::scoped_lock lock(m_mutex);
            ids = m_book.Lapse(NowDays());
            for (const auto lapsed : ids) RumorBridge::GetSingleton()->Withdraw(Rumors::Owner::Arc, lapsed, Rumors::Kind::Trouble);
            RefreshBound(m_book);   // after a load, and after a story that ended with someone still held
            // Someone on the road from another hold reaches the player: their offer goes out now (after a load too:
            // the road is counted from when their story was planned).
            {
                const float now = NowDays();
                for (auto& a : m_book.all) {
                    if (a.phase != A::Phase::Planned || !(a.flags & A::Flag::CameFar)) continue;
                    auto& pending = m_pending[a.id];
                    if (pending.offerSent) continue;
                    if (pending.offerDue < 0.0f) pending.offerDue = std::max(now, a.created + 8.0f / 24.0f);
                    if (now < pending.offerDue || LivingWorldBridge::GetSingleton()->IsBadMoment()) continue;
                    if (!A::QuietFor(m_book.LastBeat(a.id), now, A::kQuietHoursOwed) || StandsBy(a)) continue;
                    pending.offerSent = true;
                    const auto arcId = a.id;
                    const RE::FormID askerId = a.cast[A::RAsker];
                    logger::info("Arcs: #{} {} has come all the way: the offer goes out", arcId, NameOf(askerId));
                    if (auto* task = SKSE::GetTaskInterface()) {
                        task->AddTask([this, arcId, askerId]() {
                            if (auto* asker = RE::TESForm::LookupByID<RE::Actor>(askerId)) SendOffer(arcId, asker);
                        });
                    }
                }
                // Someone who could not reach the player last time and now has them in front of them again: this is
                // the next chance (the user: "If the player disappears before they can reach him, then they defer
                // until next chance to meet him").
                auto* player = RE::PlayerCharacter::GetSingleton();
                const float real = RealSeconds();
                for (auto& a : m_book.all) {
                    if (!player || a.phase != A::Phase::Planned || !(a.flags & A::Flag::Deferred)) continue;
                    auto& pending = m_pending[a.id];
                    if (pending.offerSent || (pending.deferredAt >= 0.0f && real - pending.deferredAt < 60.0f)) continue;
                    auto* asker = RE::TESForm::LookupByID<RE::Actor>(a.cast[A::RAsker]);
                    if (!asker || asker->IsDead() || !asker->Is3DLoaded() || asker->GetWorldspace() != player->GetWorldspace()) continue;
                    auto* theirCell = asker->GetParentCell();
                    auto* cell = player->GetParentCell();
                    if (!cell || !theirCell || (cell->IsInteriorCell() && cell != theirCell) || theirCell->IsInteriorCell() != cell->IsInteriorCell()) continue;
                    if (asker->GetPosition().GetDistance(player->GetPosition()) > 3000.0f) continue;
                    // The player walked up to them and spoke (they had heard the talk in town, or simply stopped for a
                    // word): then it is the player who began it, and the asker says what troubles them there and then,
                    // whoever else stands near and however lately another story moved. Otherwise the usual manners
                    // hold: not at a bad moment, not on the heels of another story, not in front of its people.
                    const bool sought = PlayerIsWith(asker, kWithDistance) && SpokeWithPlayer(asker->GetFormID());
                    std::uint8_t held = 0;
                    RE::Actor* bystander = nullptr;
                    if (LivingWorldBridge::GetSingleton()->IsBadMoment()) held = 1;
                    else if (!sought && !A::QuietFor(m_book.LastBeat(a.id), NowDays(), A::kQuietHoursOwed)) held = 2;
                    else if (!sought && (bystander = StandsBy(a)) != nullptr) held = 3;
                    if (held) {
                        if (held != pending.heldBy && held != 1) {
                            logger::info("Arcs: #{} {} is near the player and waits with the offer: {}", a.id, NameOf(a.cast[A::RAsker]),
                                         held == 2 ? "another story moved within the hour" : (NameOf(bystander->GetFormID()) + " of the story stands within earshot").c_str());
                        }
                        pending.heldBy = held;
                        continue;
                    }
                    pending.heldBy = 0;
                    pending.sought = sought;
                    pending.offerSent = true;
                    const auto arcId = a.id;
                    const RE::FormID askerId = a.cast[A::RAsker];
                    logger::info("Arcs: #{} {}: the offer goes out", arcId,
                                 sought ? "the player came to " + NameOf(askerId) + " and spoke" : NameOf(askerId) + " meets the player again");
                    if (auto* task = SKSE::GetTaskInterface()) {
                        task->AddTask([this, arcId, askerId]() {
                            if (auto* found = RE::TESForm::LookupByID<RE::Actor>(askerId)) SendOffer(arcId, found);
                        });
                    }
                }
            }
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
        // The player is busy with a quest of the journal (a stage of it moved on lately): no story cuts in.
        if (const float busy = m_questBusyUntil.load(); RealSeconds() < busy) return;
        if (m_taleBusy.load()) return;   // a tale is being written
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
        // Those who have had to do with the player come first (the order of the hour among them is kept).
        std::unordered_map<RE::FormID, int> knows;
        for (const auto* f : folk) {
            auto* who = RE::TESForm::LookupByID<RE::Actor>(f->ref);
            knows[f->ref] = who ? KnowsPlayer(who, townId) : 0;
        }
        std::stable_sort(folk.begin(), folk.end(), [&](const Folk* x, const Folk* y) { return knows[x->ref] > knows[y->ref]; });
        int tries = 0;
        const auto started = std::chrono::steady_clock::now();
        const auto spent = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count(); };
        for (const auto* f : folk) {
            if (++tries > 6) break;
            auto* asker = RE::TESForm::LookupByID<RE::Actor>(f->ref);
            if (!asker || asker->IsDead() || asker->IsDisabled()) continue;
            if (AskedWithin(asker->GetFormID(), kAskerRestDays)) continue;
            // A stranger who has heard nothing of the player does not go looking for them: only when the player is
            // right there, where a stranger with trouble might take a chance on a stranger.
            TaleSeed talk;
            if (knows[f->ref] == 0) {
                const bool there = asker->Is3DLoaded() && asker->GetWorldspace() == player->GetWorldspace() &&
                                   asker->GetParentCell() && cell && asker->GetParentCell()->IsInteriorCell() == cell->IsInteriorCell() &&
                                   (!cell->IsInteriorCell() || asker->GetParentCell() == cell) &&
                                   asker->GetPosition().GetDistance(player->GetPosition()) < 2500.0f;
                // Elsewhere in town their trouble becomes talk: the player hears of it (at the inn, from anyone) and
                // may go to them. Before, such a story was simply never told.
                if (!there) talk.rumored = true;
            }
            // A tale the dungeon master writes for them, when a model can be asked; its answer brings the offer.
            if (RequestTale(asker, {}, talk.rumored ? &talk : nullptr)) {
                logger::info("Arcs: a tale is being written in {} after {:.1f} hours there, with {} ({:.2f} ms)", PlaceName(townId),
                             (now - m_paceSince) * 24.0f, NameOf(f->ref), spent());
                m_paceSince = now;
                return;
            }
            if (talk.rumored) continue;   // no model to write it: a stranger elsewhere in town still does not come looking
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

    std::uint8_t ArcBridge::HomeOfCast(RE::TESObjectCELL* cell, const A::Arc& a, std::uint32_t roles) const {
        if (!cell || !cell->IsInteriorCell()) return 0;
        std::array<RE::Actor*, A::kRoles> actors{};
        for (std::size_t r = 1; r < A::kRoles; ++r) {
            if (!(roles & (1u << r)) || !a.cast[r]) continue;
            if (const Folk* f = FolkOf(a.cast[r]); f && f->home == cell->GetFormID()) return static_cast<std::uint8_t>(r);
            actors[r] = RE::TESForm::LookupByID<RE::Actor>(a.cast[r]);
            if (actors[r] && OwnedBy(cell->GetOwner(), actors[r])) return static_cast<std::uint8_t>(r);
        }
        // A bed of theirs is here (the player is in the cell, so its references are loaded): where they sleep is home.
        // Only furniture is asked who owns it. Asked of everything in the room, with "is this one of the faction that
        // owns it" for every person of the story, this took 0.52 ms of one frame in the Bannered Mare (2026-11-19,
        // the first time the look was timed): most of what stands in a room is no bed and falls out here.
        std::uint8_t found = 0;
        cell->ForEachReference([&](RE::TESObjectREFR* ref) {
            if (!ref || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
            auto* base = ref->GetBaseObject();
            if (!base || !base->Is(RE::FormType::Furniture)) return RE::BSContainer::ForEachResult::kContinue;
            auto* owner = ref->GetOwner();
            if (!owner) return RE::BSContainer::ForEachResult::kContinue;
            for (std::size_t r = 1; r < A::kRoles; ++r) {
                if (actors[r] && OwnedBy(owner, actors[r]) && CellAnalyzer::GetSingleton()->IsBedFurniture(ref)) {
                    found = static_cast<std::uint8_t>(r);
                    return RE::BSContainer::ForEachResult::kStop;
                }
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return found;
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
                // Only a container or a bed can be the spot: the rest is not asked who owns it (the dear part).
                if (!base || (!base->Is(RE::FormType::Container) && !base->Is(RE::FormType::Furniture))) return RE::BSContainer::ForEachResult::kContinue;
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
        const auto began = FrameClock::Now();
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || !A::IsOpen(*a)) return 0;
        FrameClock cost("Arcs: a look at a story", g_lookCost, began);
        auto& pending = m_pending[id];
        A::Facts f;
        std::array<RE::Actor*, A::kRoles> actors{};
        const bool scene = RealSeconds() < pending.sceneUntil;
        for (std::size_t r = 1; r < A::kRoles; ++r) {
            if (!a->cast[r]) continue;
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(a->cast[r]);
            actors[r] = actor;
            f.dead[r] = !actor || actor->IsDead() || actor->IsDeleted();
            f.with[r] = actor && PlayerIsWith(actor, kWithDistance);
            if (f.with[r]) {
                const float since = SpeechTracker::GetSingleton()->SecondsSinceSpeechEnded(a->cast[r]);
                f.spoke[r] = SpokeWithPlayer(a->cast[r]) || (scene && since >= 0.0f && since <= kSpokeSeconds);
            }
        }
        for (std::size_t p = 0; p < A::kProps; ++p) {
            f.has[p] = a->propBase[p] && FindStamped(player, a->propBase[p], a->propName[p]).object != nullptr;
            f.handed[p] = (pending.handed & (1u << p)) != 0;
        }
        const auto nodes = A::NodesOf(*a);
        // A new step: what was read or held for the one before is gone.
        if (pending.node != a->node) {
            pending.node = a->node;
            pending.turn = -1;
            pending.talked = false;
            pending.judging = false;
            pending.askedAt = -1.0f;
            pending.nones = 0;
            pending.visitAt = -1.0f;
            pending.lingering = false;
        }
        auto* cell = player->GetParentCell();
        const bool tale = a->pattern == A::Pattern::Tale;
        // Whose home of the cast this interior is, looked at once per cell the player enters (a scan of its beds).
        if (cell && cell->GetFormID() != pending.checkedCell) {
            pending.checkedCell = cell->GetFormID();
            pending.homeOf = 0;
            pending.homeHere = false;
            if (cell->IsInteriorCell()) {
                std::uint32_t wanted = 0;
                for (std::size_t r = 1; r < A::kRoles; ++r) {
                    const bool want = tale ? (r != A::RAsker || (a->node < nodes.size() && nodes[a->node].who == r)) : r == A::RCulprit;
                    if (a->cast[r] && want) wanted |= 1u << r;
                }
                const auto scanStart = std::chrono::steady_clock::now();
                pending.homeOf = wanted ? HomeOfCast(cell, *a, wanted) : 0;
                pending.homeHere = !tale && pending.homeOf == A::RCulprit;
                const double scanMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scanStart).count();
                if (scanMs > 0.1) logger::info("Arcs: #{} whose home is {:08X}: {} ({:.2f} ms)", id, cell->GetFormID(), pending.homeOf, scanMs);
            }
        }
        if (a->scene) {
            f.atScene = cell && cell->GetFormID() == a->scene;   // the victim's home
        } else if (tale && a->node < nodes.size() && nodes[a->node].kind == A::Kind::Reach) {
            f.atScene = pending.homeOf != 0 && pending.homeOf == nodes[a->node].who;   // the step's person's home
        }
        f.answer = pending.answer;
        f.turn = pending.turn;
        f.brawl = pending.brawl;
        f.speech = static_cast<int>(player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech));
        // The prop is with the one it goes to, however it got there: the popup, or their own TakeItem action in talk
        // ("give it here": SeverActions moved Ysolda's ring out of the player's pack, and the hand-over never came,
        // 2026-11-18), or a gift through the game's own menus.
        if (a->node < nodes.size()) {
            A::Role to = A::RNone;
            std::uint8_t prop = 0;
            if (HandOverOf(nodes[a->node], to, prop) && !f.has[prop] && actors[to] &&
                FindStamped(actors[to], a->propBase[prop], a->propName[prop]).object) {
                f.handed[prop] = true;
            }
        }
        // A tale's places out in the world: the player at a place's heart, its boss dead.
        if (tale) {
            for (std::size_t s = 0; s < A::kSites; ++s) {
                if (!a->site[s]) continue;
                auto* anchor = a->siteAnchor[s] ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a->siteAnchor[s]) : nullptr;
                if (InTheWorldNow(anchor)) {
                    auto* anchorCell = anchor->GetParentCell();
                    const bool sameSpace = anchorCell && cell && (anchorCell->IsInteriorCell() ? anchorCell == cell
                                                                                              : anchor->GetWorldspace() == player->GetWorldspace());
                    const float reach = anchorCell && anchorCell->IsInteriorCell() ? 1800.0f : 2500.0f;
                    // The heart of some places is their own master (Halted Stream Camp's chief): where the game put
                    // him, not where a fight has since taken him (he ran to the way in, and the way in was "the depths").
                    auto heartAt = anchor->GetPosition();
                    if (auto* master = anchor->As<RE::Actor>()) {
                        RE::NiPoint3 pos, rot;
                        RE::TESForm* where = nullptr;
                        master->GetEditorLocation2(pos, rot, where, nullptr);
                        if (where && where == anchorCell) heartAt = pos;
                    }
                    f.atDepths[s] = sameSpace && heartAt.GetDistance(player->GetPosition()) < reach;
                }
                // Reached once is reached, whichever step the story was on at the time (bit 2 of the place's state,
                // kept with the story): a player who went down there before the journal asked is not sent down again.
                if (f.atDepths[s]) a->siteState[s] |= 4u;
                else if (a->siteState[s] & 4u) f.atDepths[s] = true;
                auto* boss = a->siteBoss[s] ? RE::TESForm::LookupByID<RE::Actor>(a->siteBoss[s]) : nullptr;
                // Dead only once it was seen alive in a loaded cell, or found dead at the heart by the player: an actor
                // placed in an unloaded camp reads as dead or vanishes before anyone got there (Vargr the Red, 2026-11-19).
                const bool loadedAlive = boss && boss->Is3DLoaded() && !boss->IsDead();
                // Seen alive is kept with the story (bit 3 of the place's state), not only for this session: a master
                // killed away from the heart of its place, a save, a load, and the kill counted for nothing until the
                // player had been down to the heart (the user asked what happens when it dies before its step).
                if (loadedAlive) a->siteState[s] |= 8u;
                if (a->siteState[s] & 8u) pending.bossSeenAlive[s] = true;
                const std::int8_t state = !a->siteBoss[s] ? 0 : !boss ? 3 : boss->IsDead() ? 2 : loadedAlive ? 1 : 0;
                if (state != pending.bossState[s]) {
                    pending.bossState[s] = state;
                    logger::info("Arcs: #{} the master of {}: {}", id, NameOf(a->site[s]),
                                 state == 1 ? "alive, here" : state == 2 ? "dead" : state == 3 ? "gone from the game" : "not here yet");
                }
                if (!a->siteBoss[s]) f.bossDead[s] = f.atDepths[s];                       // none could be placed: reaching it is enough
                else if (!boss) f.bossDead[s] = f.atDepths[s];                            // gone: the heart reached is the end of it
                else f.bossDead[s] = boss->IsDead() && (pending.bossSeenAlive[s] || f.atDepths[s]);
            }
        }
        // Someone held at a place is free once the player has cut them loose with their own hand (Activated, below),
        // whichever step the story was on at the time: the step that frees them is done the moment the story comes to
        // it, and nobody is left kneeling in their bonds beside the one who came for them.
        if (tale && a->captive) f.captiveReached = a->captiveState >= A::CaptiveFreed;
        // What was left of a victim outside a place: looked at once is looked at, whichever step was current then
        // (the player's hand on it, Activated). One that has gone again (the game clears its dead away) is laid anew
        // while the story still wants it.
        if (tale) {
            for (std::size_t k = 0; k < A::kClues; ++k) {
                if (!a->clueWhat[k]) continue;
                if (a->clueState[k] == A::CluePlaced) {
                    auto* lies = a->clueRef[k] ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a->clueRef[k]) : nullptr;
                    if (!lies || lies->IsDeleted() || lies->IsDisabled()) {
                        logger::info("Arcs: #{} what lay outside {} is gone: laid anew when the player is next there", id, NameOf(a->site[a->clueSite[k] - 1u]));
                        a->clueState[k] = A::ClueToPlace;
                        a->clueRef[k] = 0;
                    }
                }
                f.examined[k] = a->clueState[k] == A::ClueSeen;
            }
            // What walks by night: struck down is struck down, by whoever's hand; one the game has lost walks again
            // the next night. Whether the player's hand on it would lay it to rest is kept for the crosshair.
            if (a->spiritSite) {
                if (a->spiritState == A::SpiritOut) {
                    auto* shade = a->spiritRef ? RE::TESForm::LookupByID<RE::Actor>(a->spiritRef) : nullptr;
                    if (!shade || shade->IsDeleted()) {
                        logger::info("Arcs: #{} {} is gone from the game: it walks again when night and the player are there", id, a->spiritName);
                        a->spiritState = A::SpiritAway;
                        a->spiritRef = 0;
                    } else if (shade->IsDead()) {
                        logger::info("Arcs: #{} {} is struck down outside {}", id, a->spiritName, NameOf(a->site[a->spiritSite - 1u]));
                        a->spiritState = A::SpiritBanished;
                    }
                }
                f.spirit = a->spiritState;
                const bool out = a->spiritState == A::SpiritOut && a->spiritRef;
                SetSpirit(a->id, out ? a->spiritRef : 0u, out && a->spiritWants && f.has[a->spiritWants - 1u]);
            }
        }
        // A tale's ambush: its men are down, each dead or gone from the game, and at least one fell (they are placed
        // beside the player, in loaded ground: a death there is a real one).
        const bool ambushStep = tale && a->node < nodes.size() && nodes[a->node].kind == A::Kind::Ambush;
        int thugsPlaced = 0;
        if (ambushStep) {
            int dead = 0, gone = 0;
            for (std::size_t k = 0; k < A::kThugs; ++k) {
                if (!a->thug[k]) continue;
                ++thugsPlaced;
                auto* t = RE::TESForm::LookupByID<RE::Actor>(a->thug[k]);
                if (!t || t->IsDeleted()) ++gone;
                else if (t->IsDead()) ++dead;
            }
            f.ambushOver = thugsPlaced > 0 && dead > 0 && dead + gone == thugsPlaced;
            // Beaten down (the script keeps the player from dying while the men are on them): the step's "lost".
            if (pending.struck && player->AsActorState()->IsBleedingOut()) f.brawl = -1;
            // What the talk settled on costs gold the player may not have: then nothing is settled.
            if (pending.turn >= 0) {
                for (const auto& e : nodes[a->node].edges) {
                    if (static_cast<int>(e.when) != static_cast<int>(A::If::Said0) + pending.turn) continue;
                    const bool pays = e.effect == A::Effect::PlayerPays || e.effect2 == A::Effect::PlayerPays;
                    const auto price = static_cast<std::int32_t>(a->price ? a->price : a->gold);
                    if (pays && player->GetGoldAmount() < price) {
                        logger::info("Arcs: #{} the ambush: {} gold was offered and the player has {}", id, price, player->GetGoldAmount());
                        pending.turn = -1;
                        f.turn = -1;
                        pending.parleyFight = true;
                        pending.shortPurse = true;
                    }
                }
            }
        }
        const std::uint8_t nodeBefore = a->node;
        if (tale) TaleTalk(id, *a, pending, f);
        const auto change = A::Advance(*a, f, now);
        pending.answer = -1;
        std::int32_t bits = 0;
        // The ambush's step was left with men still standing (the player beaten, or they were talked out of it): they
        // stand down and leave.
        if (ambushStep && (a->node != nodeBefore || change.ending != A::Ending::None)) {
            const bool standing = std::any_of(a->thug.begin(), a->thug.end(), [](std::uint32_t ref) {
                auto* man = ref ? RE::TESForm::LookupByID<RE::Actor>(ref) : nullptr;
                return man && !man->IsDeleted() && !man->IsDead();
            });
            if (standing) {
                pending.ambushDo = 6;
                pending.standWhy = f.brawl < 0 ? 1 : 2;
                logger::info("Arcs: #{} the ambush: {}; the men stand down and leave", id,
                             f.brawl < 0 ? "the player is beaten" : "they were talked out of it");
            }
        }
        if (change.any()) {
            // The step done, plus one (one step a look at most): bits 0-4.
            if (!change.done.empty()) bits |= (static_cast<std::int32_t>(change.done.front()) + 1) & 0x1F;
            bits |= static_cast<std::int32_t>(change.ending) << 8;
            bits |= (static_cast<std::int32_t>(change.effect) & 0xF) << 16;
            bits |= (static_cast<std::int32_t>(change.effectRole) & 0xF) << 20;
            if (change.skipped != A::kNoNode) bits |= ((static_cast<std::int32_t>(change.skipped) + 1) & 0xF) << 27;
            if (change.stayed) bits |= 1 << 15;   // stayed on the step: a line to write, nothing done
            pending.log = change.log;
            pending.effect2 = static_cast<int>(change.effect2);
            pending.effect2Role = static_cast<int>(change.effect2Role);
            pending.bondWith = static_cast<int>(change.bondWith);
            pending.effectProp = static_cast<int>(change.effectProp);
            pending.handed = 0;
            pending.brawl = 0;
            // A new step asks afresh: the hold after an answer was the step's before (Ulfberth's peace popup waited two
            // minutes after Ysolda's decision, 2026-11-18).
            pending.chooseHold = -1.0f;
            pending.giveHold = -1.0f;
            // Still on the same step (refused): not asked again at once.
            if (change.stayed) pending.chooseHold = RealSeconds() + kHoldSeconds;
            logger::info("Arcs: #{} step done {}, now at step {}{}{}", id, (bits & 0x1F) - 1, static_cast<int>(a->node),
                         change.effect != A::Effect::None ? ", effect " + std::to_string(static_cast<int>(change.effect)) : std::string(),
                         change.ending != A::Ending::None ? ", ended " + std::to_string(static_cast<int>(change.ending)) : std::string());
            if (change.ending != A::Ending::None) {
                Ended(*a);
                if (pending.ambushDo) bits |= 1 << 13;
                return bits;
            }
        }
        // A prop hidden in someone's home goes there once the player is in it (its references are loaded only then);
        // nothing is put anywhere before the arc is taken on. The patterns hide their first prop with the culprit.
        const bool homeProp = a->pattern == A::Pattern::Thief || a->pattern == A::Pattern::Murder;
        for (std::size_t p = 0; p < A::kProps && pending.plantProp < 0 && pending.homeOf; ++p) {
            const std::uint8_t hider = tale ? a->propHome[p] : (homeProp && p == 0 ? static_cast<std::uint8_t>(A::RCulprit) : 0);
            if (!hider || hider != pending.homeOf || !a->propBase[p] || a->propAt[p]) continue;
            const auto spot = HidingSpot(cell, actors[hider]);
            if (!spot.ref) continue;
            pending.plantAt = spot.ref->GetFormID();
            pending.plantProp = static_cast<int>(p);
            pending.plantFloor = spot.floor;
            bits |= 1 << 24;
            logger::info("Arcs: #{} '{}' goes {} {:08X} in {}'s home", id, a->propName[p], spot.floor ? "beside" : "into",
                         spot.ref->GetFormID(), NameOf(a->cast[hider]));
        }
        // The journal's marker moves with what is in the game, for every kind of step (MarkerOf): the place on the map,
        // nothing while inside, the heart, the master, the body, the one held; a home's door until what is hidden there
        // is there; a person, or their door while they are not in the game. Said to the script when it has moved; a
        // new step sets its own (ShowStep).
        if (a->node < nodes.size()) {
            auto* marker = MarkerOf(*a, nodes[a->node]);
            const RE::FormID now = marker ? marker->GetFormID() : 0u;
            if (!pending.siteMarkerKnown || pending.markerNode != a->node) {
                pending.siteMarkerKnown = true;
                pending.markerNode = a->node;
                pending.siteMarker = now;
            } else if (now != pending.siteMarker) {
                logger::info("Arcs: #{} the journal now points at {:08X} (was {:08X}), step {}", id, now, pending.siteMarker, static_cast<int>(a->node));
                pending.siteMarker = now;
                bits |= 1 << 14;
            }
        }
        if (tale) {
            // A heart that came into the game only now (its part of the place was loaded): the story's master is
            // put there at once. The script places (PlaceBosses, out of the player's sight where that can be helped)
            // and sets the journal's marker anew; asked again every look until it is done.
            for (std::size_t s = 0; s < A::kSites; ++s) {
                if (!a->site[s] || (a->siteState[s] & 1u)) continue;
                bool needed = false;
                for (const auto& step : a->graph) needed = needed || (step.kind == A::Kind::Slay && step.site == s + 1);
                for (const auto p : a->propSite) needed = needed || p == s + 1;
                auto* anchor = a->siteAnchor[s] ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a->siteAnchor[s]) : nullptr;
                if (needed && InTheWorldNow(anchor)) bits |= 1 << 14;
            }
            // What is to be looked at outside a place and does not lie there yet: laid now that the ground by its way
            // in is in the game (the script's PlaceClues; asked again every look until it is done).
            for (std::size_t k = 0; k < A::kClues; ++k) {
                if (!a->clueWhat[k] || a->clueState[k] != A::ClueToPlace || !ClueWanted(*a, k)) continue;
                if (InTheWorldNow(OutsideOf(*a, a->clueSite[k]))) bits |= 1 << 14;
            }
            // What walks by night: put there once it is night and the ground by the place is in the game, taken away
            // at first light (the script's TendSpirit; asked again every look until it is done).
            if (SpiritWanted(*a)) {
                const bool night = A::IsNight(HourNow());
                if (a->spiritState == A::SpiritAway && night && InTheWorldNow(OutsideOf(*a, a->spiritSite))) bits |= 1 << 14;
                if (a->spiritState == A::SpiritOut && !night) bits |= 1 << 14;
            }
            // Someone held there and still away: they are put at the heart of the place now that it is in the game
            // (the script's PlaceCaptive; asked again every look until it is done).
            if (a->captive && a->captiveSite && a->captiveState == A::CaptiveAway) {
                const auto s = static_cast<std::size_t>(a->captiveSite - 1u);
                auto* anchor = a->siteAnchor[s] ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a->siteAnchor[s]) : nullptr;
                if (InTheWorldNow(anchor)) bits |= 1 << 14;
            }
            for (std::size_t s = 0; s < A::kSites; ++s) {
                if (!a->site[s] || !(a->siteState[s] & 1u) || (a->siteState[s] & 2u)) continue;
                auto* anchor = a->siteAnchor[s] ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a->siteAnchor[s]) : nullptr;
                if (!InTheWorldNow(anchor)) continue;
                // The place's heart is loaded: a place the player emptied before gets company for its new master.
                a->siteState[s] |= 2u;
                auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(a->site[s]);
                if (!location || !location->IsCleared()) continue;
                // The script places them (PlaceGang): the game's leveled lists are resolved by Papyrus' PlaceAtMe only.
                if (!pending.gangSite) {
                    pending.gangSite = static_cast<std::uint8_t>(s + 1);
                    logger::info("Arcs: #{} {} had been cleared: company comes back with its new master", id, NameOf(a->site[s]));
                }
            }
            if (pending.gangSite) bits |= 1 << 7;   // asked again every look until the script says it is done
            // A prop the ambush's leader carries goes into their pack once they are placed and loaded.
            for (std::size_t p = 0; p < A::kProps && pending.plantProp < 0; ++p) {
                if (!(a->propAmbush & (1u << p)) || a->propAt[p] || !a->propBase[p] || !a->thug[0]) continue;
                auto* leader = RE::TESForm::LookupByID<RE::Actor>(a->thug[0]);
                if (!leader || !leader->Is3DLoaded()) continue;
                pending.plantAt = leader->GetFormID();
                pending.plantProp = static_cast<int>(p);
                pending.plantFloor = false;
                bits |= 1 << 24;
                logger::info("Arcs: #{} '{}' goes into the pack of the ambush's leader {:08X}", id, a->propName[p], leader->GetFormID());
            }
            // A prop the boss carries goes into their pack once they are loaded (an unloaded actor's pack never got it).
            for (std::size_t p = 0; p < A::kProps && pending.plantProp < 0; ++p) {
                if (!a->propSite[p] || a->propAt[p] || !a->propBase[p]) continue;
                auto* boss = RE::TESForm::LookupByID<RE::Actor>(a->siteBoss[a->propSite[p] - 1u]);
                if (!boss || !boss->Is3DLoaded()) continue;
                pending.plantAt = boss->GetFormID();
                pending.plantProp = static_cast<int>(p);
                pending.plantFloor = false;
                bits |= 1 << 24;
                logger::info("Arcs: #{} '{}' goes into the pack of {}", id, a->propName[p], NameOf(boss->GetFormID()));
            }
        }
        if (a->node < nodes.size() && !change.any()) {
            const auto& n = nodes[a->node];
            // The step's person has started talking with the player: they stay with the player until the talk is over
            // (the user, 2026-11-18: "Only if the player doesnt respond for a long time, the npc gets bored and walks
            // off"). Jon Battle-Born walked home in the middle of being asked about a goblet (2026-11-19).
            if (n.who && n.kind != A::Kind::Wait && n.kind != A::Kind::Have && n.kind != A::Kind::Ambush && n.kind != A::Kind::Stalk && n.kind != A::Kind::Free && f.with[n.who] && f.spoke[n.who] && !f.dead[n.who] &&
                !pending.lingering && RealSeconds() >= pending.lingerHold) {
                pending.lingering = true;
                bits |= 1 << 6;
                logger::info("Arcs: #{} {} is talking with the player: asked to stay near (step {})", id, NameOf(a->cast[n.who]), static_cast<int>(a->node));
            }
            // A visit: the step's person is sent to the player (again after a couple of game hours if they could not
            // come); when they are already here the script has them speak up.
            // Nobody crosses Skyrim inside a town's story: a visitor or a shadow comes when the player is in the
            // tale's own hold (or they are near anyway). An old companion, who is of no town, comes from wherever
            // their life has them: that journey is their part in it.
            const auto withinReach = [&](std::size_t role) {
                auto* who = actors[role];
                if (!who) return false;
                if (NPCIndex::ReachOf(who) == 0) return true;   // with the player, or of the place the player is in
                const Folk* folk = FolkOf(a->cast[role]);
                if (!folk || folk->town != a->place) return true;
                auto* playerHold = HoldOf(player->GetCurrentLocation());
                auto* taleHold = HoldOf(RE::TESForm::LookupByID<RE::BGSLocation>(a->place));
                return playerHold && taleHold && playerHold == taleHold;
            };
            if ((n.kind == A::Kind::Visit || n.kind == A::Kind::Stalk) && n.who && !f.dead[n.who] && !withinReach(n.who)) {
                if (!pending.tooFar) {
                    pending.tooFar = true;
                    logger::info("Arcs: #{} {} does not come after the player across Skyrim: step {} waits until the player is back in {}", id,
                                 NameOf(a->cast[n.who]), static_cast<int>(a->node), NameOf(a->place));
                }
            } else if (n.kind == A::Kind::Visit && n.who && !f.dead[n.who] && !(f.with[n.who] && f.spoke[n.who]) &&
                (pending.visitAt < 0.0f || now - pending.visitAt > 2.0f / 24.0f || now < pending.visitAt)) {
                pending.tooFar = false;
                pending.visitAt = now;
                bits |= 1 << 5;
                logger::info("Arcs: #{} {} is to come to the player (step {})", id, NameOf(a->cast[n.who]), static_cast<int>(a->node));
            }
            const float real = RealSeconds();
            // Someone shadowing the player: sent after them (the visit's signal; the script sees the step's kind) when
            // the player is out of doors, in no fight, and not standing next to them already. Tried again every couple
            // of minutes while it cannot be (another visitor on the way, indoors).
            if (n.kind == A::Kind::Stalk && n.who && !f.dead[n.who] && !f.with[n.who] && withinReach(n.who) && real >= pending.stalkHold && cell &&
                !cell->IsInteriorCell() && !player->IsInCombat() && !LivingWorldBridge::GetSingleton()->IsBadMoment() &&
                real >= m_questBusyUntil.load()) {
                auto* shadow = actors[n.who];
                const bool inReach = shadow && shadow->Is3DLoaded() && shadow->GetWorldspace() == player->GetWorldspace();
                const auto to = inReach ? shadow->GetPosition() - player->GetPosition() : RE::NiPoint3{};
                const float distance = inReach ? std::sqrt(to.x * to.x + to.y * to.y) : 1.0e9f;
                const bool beside = inReach && distance < 2500.0f;
                // From behind only: someone who would come at the player from the front, in plain view, is no shadow
                // (Sven crouched in the gateway the player was walking toward, 2026-11-19). Angle 0 is north, clockwise.
                const float facing = player->GetAngleZ();
                const bool ahead = inReach && distance > 1.0f && (to.x * std::sin(facing) + to.y * std::cos(facing)) / distance > 0.17f;
                if (!beside && !ahead) {
                    pending.stalkHold = real + 120.0f;
                    bits |= 1 << 5;
                    logger::info("Arcs: #{} {} is to shadow the player (step {})", id, NameOf(a->cast[n.who]), static_cast<int>(a->node));
                }
            }
            if ((n.kind == A::Kind::Choose || n.kind == A::Kind::Brawl) && n.who && f.spoke[n.who] && !f.dead[n.who] &&
                real >= pending.chooseHold) {
                pending.chooseHold = real + 30.0f;   // the popup is up; Answer sets the real hold
                bits |= 1 << 25;
                logger::info("Arcs: #{} step {}: {} has talked with the player; the decision is offered", id, static_cast<int>(a->node),
                             NameOf(a->cast[n.who]));
            }
            if (n.kind == A::Kind::Ambush) AmbushLook(id, *a, pending, n, now, thugsPlaced);
            A::Role to = A::RNone;
            std::uint8_t prop = 0;
            if (HandOverOf(n, to, prop) && f.has[prop] && f.spoke[to] && !f.dead[to] && real >= pending.giveHold && !(bits & (1 << 25))) {
                pending.giveHold = real + kHoldSeconds;
                bits |= 1 << 26;
                logger::info("Arcs: #{} step {}: the hand-over to {} is offered", id, static_cast<int>(a->node), NameOf(a->cast[to]));
            }
        }
        if (pending.ambushDo) bits |= 1 << 13;   // said again every look until the script has done it
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
        const auto nodes = A::NodesOf(*a);
        A::Role role = A::RNone;
        std::uint8_t prop = 0;
        if (a->node >= nodes.size() || !HandOverOf(nodes[a->node], role, prop)) return false;
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

    namespace {
        // A quest of the journal moving on (the main quest, a faction's, a Daedric prince's, the DLCs', a side quest):
        // the player is in the middle of it, and no town story cuts in for a while (the user, 2026-11-18: "the
        // player doesn't get interrupted while doing main quests").
        constexpr float kQuestBusySeconds = 15.0f * 60.0f;
        class QuestStageSink : public RE::BSTEventSink<RE::TESQuestStageEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESQuestStageEvent* e, RE::BSTEventSource<RE::TESQuestStageEvent>*) override {
                if (!e) return RE::BSEventNotifyControl::kContinue;
                auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(e->formID);
                if (!quest) return RE::BSEventNotifyControl::kContinue;
                const auto type = quest->GetType();
                if (type == RE::QUEST_DATA::Type::kNone || type == RE::QUEST_DATA::Type::kMiscellaneous) return RE::BSEventNotifyControl::kContinue;
                const char* id = quest->GetFormEditorID();
                if (id && std::string_view(id).starts_with("IntelEngine")) return RE::BSEventNotifyControl::kContinue;
                ArcBridge::GetSingleton()->QuestMovedOn(quest);
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    namespace {
        // The player lays hands on someone (activates them). The game sends this for every activation by anyone, of
        // anything: all but the player's on an actor fall out at once, so it costs nothing while playing.
        class ActivateSink : public RE::BSTEventSink<RE::TESActivateEvent> {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* e, RE::BSTEventSource<RE::TESActivateEvent>*) override {
                if (!e || !e->actionRef || !e->objectActivated || !e->actionRef->IsPlayerRef()) return RE::BSEventNotifyControl::kContinue;
                if (auto* actor = e->objectActivated->As<RE::Actor>()) ArcBridge::GetSingleton()->Activated(actor);
                return RE::BSEventNotifyControl::kContinue;
            }
        };
    }

    namespace {
        // The crosshair's word on someone a story holds bound: "Cut loose" where the game would say "Talk" (the user,
        // 2026-11-19: "Yes, relabel"). Skyrim's scripts cannot change that word on a reference, so the game's own
        // answer for an NPC is taken and its first line replaced, for the one held and nobody else. The word is the
        // name of a message of IntelEngine.esp (IntelEngine_CutLoose, esp/EspBuilder), so a translation of the plugin
        // file translates it. Asked whenever the player looks at someone: four numbers compared, no lock.
        std::string g_cutLoose = "Cut loose";
        std::string g_examine = "Examine";   // on what was left of a victim, where the game would say "Search"
        std::string g_layToRest = "Lay to rest";   // on what walks by night, while the player carries what it wants

        struct NpcActivateText {
            static bool thunk(RE::TESNPC* base, RE::TESObjectREFR* ref, RE::BSString& text) {
                const bool shown = func(base, ref, text);
                if (!shown || !ref) return shown;
                const RE::FormID id = ref->GetFormID();
                const std::string* word = nullptr;
                for (const auto& one : g_bound) if (one.load(std::memory_order_relaxed) == id) word = &g_cutLoose;
                for (const auto& one : g_clues) if (one.load(std::memory_order_relaxed) == id) word = &g_examine;
                for (const auto& one : g_spirits) {
                    if (one.ref.load(std::memory_order_relaxed) == id && one.rest.load(std::memory_order_relaxed)) word = &g_layToRest;
                }
                if (!word) return shown;
                // The game writes "<word>\n<name>": the name is kept as it stands.
                const char* was = text.c_str();
                const char* name = was ? std::strchr(was, '\n') : nullptr;
                std::string now = *word;
                if (name) {
                    now += name;
                } else {
                    const char* display = ref->GetDisplayFullName();
                    now += '\n';
                    now += display ? display : "";
                }
                text = now.c_str();
                return shown;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    // Someone a story holds bound is freed by the player's own hand: they walk up and activate them (the user,
    // 2026-11-19: "freeing should be activate"). Coming within a few steps used to free them, wanted or not: sneaking
    // past, or with their captors still standing around. Now nothing happens until the player does it, and then at
    // once: the state here, and a word to the script, which takes the bonds off without waiting for its next look.
    // In the middle of a fight too, at the player's own risk: the one freed still cannot die and is still left alone
    // by the people of the place until they part from the player (the script's UnholdCaptive, keepSafe).
    void ArcBridge::Activated(RE::Actor* actor) {
        if (!actor || actor->IsDisabled()) return;
        std::uint32_t freed = 0, looked = 0, touched = 0;
        bool canRest = false;
        std::size_t clue = 0;
        {
            std::scoped_lock lock(m_mutex);
            for (auto& a : m_book.all) {
                if (!A::IsOpen(a)) continue;
                // What was left of a victim: the player kneels by it and looks closely. Theirs from now on, whichever
                // step the story is on (a player who comes on it first has still seen what they saw).
                for (std::size_t k = 0; k < A::kClues && !looked; ++k) {
                    if (!a.clueWhat[k] || a.clueState[k] != A::CluePlaced || a.clueRef[k] != actor->GetFormID()) continue;
                    a.clueState[k] = A::ClueSeen;
                    const auto nodes = A::NodesOf(a);
                    const bool asked = a.node < nodes.size() && nodes[a.node].kind == A::Kind::Examine && nodes[a.node].prop == k;
                    logger::info("Arcs: #{} the player looks closely at what was left outside {}{}", a.id, NameOf(a.site[a.clueSite[k] - 1u]),
                                 asked ? "" : " (before the story asked for it)");
                    looked = a.id;
                    clue = k;
                }
                if (looked) break;
                if (actor->IsDead() || !a.captive || a.captiveState != A::CaptiveBound || a.cast[a.captive] != actor->GetFormID()) continue;
                a.captiveState = A::CaptiveFreed;
                const auto nodes = A::NodesOf(a);
                const bool asked = a.node < nodes.size() && nodes[a.node].kind == A::Kind::Free;
                logger::info("Arcs: #{} {} is cut loose by the player{}", a.id, NameOf(actor->GetFormID()),
                             asked ? "" : " (before the story asked for it)");
                freed = a.id;
                break;
            }
            if (freed || looked) RefreshBound(m_book);
            // What walks by night: the player reaches out to it. With what it wants in their pack that lays it to
            // rest (the script's part: its last words, and it goes); without, their hand passes through it.
            if (!freed && !looked && !actor->IsDead()) {
                auto* player = RE::PlayerCharacter::GetSingleton();
                for (const auto& a : m_book.all) {
                    if (!A::IsOpen(a) || a.spiritState != A::SpiritOut || a.spiritRef != actor->GetFormID()) continue;
                    touched = a.id;
                    if (a.spiritWants && player) {
                        const auto p = static_cast<std::size_t>(a.spiritWants - 1u);
                        canRest = FindStamped(player, a.propBase[p], a.propName[p]).object != nullptr;
                    }
                    break;
                }
            }
        }
        if (touched) {
            if (auto* source = SKSE::GetModCallbackEventSource()) {
                SKSE::ModCallbackEvent event{};
                event.eventName = "IntelEngine_ArcSpirit";
                event.strArg = canRest ? "rest" : "touch";
                event.numArg = static_cast<float>(touched);
                event.sender = actor;
                source->SendEvent(&event);
            }
            return;
        }
        if (!freed && !looked) return;
        if (auto* source = SKSE::GetModCallbackEventSource()) {
            SKSE::ModCallbackEvent event{};
            event.eventName = freed ? "IntelEngine_ArcUntie" : "IntelEngine_ArcClue";
            event.strArg = freed ? "" : std::to_string(clue).c_str();
            event.numArg = static_cast<float>(freed ? freed : looked);
            event.sender = actor;
            source->SendEvent(&event);
        }
    }

    void ArcBridge::QuestMovedOn(RE::TESQuest* quest) {
        const float until = RealSeconds() + kQuestBusySeconds;
        if (until - m_questBusyUntil.load() > 60.0f) {
            const char* name = quest ? quest->GetFullName() : nullptr;
            logger::info("Arcs: the player is on a quest ({}); no town story for {:.0f} minutes", name && *name ? name : "a quest",
                         kQuestBusySeconds / 60.0f);
        }
        m_questBusyUntil.store(until);
    }

    void ArcBridge::Listen() {
        static GoldSink sink;
        static QuestStageSink stages;
        static ActivateSink hands;
        static bool listening = false;
        if (listening) return;
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESContainerChangedEvent>(&sink);
            events->AddEventSink<RE::TESQuestStageEvent>(&stages);
            events->AddEventSink<RE::TESActivateEvent>(&hands);
            // The crosshair's word for someone held (once, at the main menu: the plugin files are read by now).
            if (auto* data = RE::TESDataHandler::GetSingleton()) {
                auto* message = data->LookupForm<RE::BGSMessage>(0x58E5, "IntelEngine.esp");
                const char* word = message ? message->GetFullName() : nullptr;
                if (word && *word) g_cutLoose = word;
                auto* look = data->LookupForm<RE::BGSMessage>(0x58E6, "IntelEngine.esp");
                const char* lookWord = look ? look->GetFullName() : nullptr;
                if (lookWord && *lookWord) g_examine = lookWord;
                auto* rest = data->LookupForm<RE::BGSMessage>(0x58E7, "IntelEngine.esp");
                const char* restWord = rest ? rest->GetFullName() : nullptr;
                if (restWord && *restWord) g_layToRest = restWord;
                logger::info("Arcs: the crosshair's word for someone held bound: \"{}\"{}", g_cutLoose, message ? "" : " (no such message in IntelEngine.esp)");
            }
            REL::Relocation<std::uintptr_t> npc{RE::VTABLE_TESNPC[0]};
            NpcActivateText::func = npc.write_vfunc(0x4C, NpcActivateText::thunk);
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

    void ArcBridge::Scene(std::uint32_t id, float seconds) {
        std::scoped_lock lock(m_mutex);
        if (!m_book.Find(id)) return;
        m_pending[id].sceneUntil = RealSeconds() + std::clamp(seconds, 0.0f, 300.0f);
        logger::info("Arcs: #{} a scene in front of the player for {:.0f} s", id, seconds);
    }

    bool ArcBridge::Brawl(std::uint32_t id, int result) {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a || !A::IsOpen(*a) || result == 0) return false;
        m_pending[id].brawl = result > 0 ? 1 : -1;
        logger::info("Arcs: #{} brawl {}", id, result > 0 ? "won" : "lost");
        return true;
    }

    void ArcBridge::LingerOver(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        if (auto it = m_pending.find(id); it != m_pending.end()) {
            it->second.lingering = false;
            it->second.lingerHold = RealSeconds() + 60.0f;   // a refusal (a scene) or a talk just over: not at once again
        }
    }

    // The game's own creatures for a place's heart and its company, by what lives there (Tales::SiteKind).
    RE::TESBoundObject* ArcBridge::BossBase(std::uint8_t kind, std::uint32_t seed) {
        auto* data = RE::TESDataHandler::GetSingleton();
        const auto form = [&](RE::FormID id) -> RE::TESBoundObject* {
            auto* f = data ? data->LookupForm(id, "Skyrim.esm") : nullptr;
            return f ? f->As<RE::TESBoundObject>() : nullptr;
        };
        switch (kind) {
            case Tales::SiteDwemer: return form(0x10F9B9);                              // Dwarven centurion
            case Tales::SiteFalmer: return form(0x5238F);                               // Falmer chief (leveled)
            case Tales::SiteCrypt: return form(0xDD9D8);                                // draugr overlord, no dragon priest
            case Tales::SiteBandits: return form(seed % 3u == 0u ? 0x1A340 : 0x1A341);  // a bandit chief
            case Tales::SiteDen: return form(seed % 2u ? 0x23AAB : 0x23ABA);           // giant frostbite spider, troll
            case Tales::SiteVampires: return form(0x339A9);                             // vampire (leveled boss)
            case Tales::SiteNecromancers: return form(seed % 2u ? 0xE106D : 0xE2217);  // necromancer
            case Tales::SiteForsworn: return form(0x442FD);                             // Forsworn shaman
            case Tales::SiteHagraven: return form(0x640C0);                             // hagraven
            case Tales::SiteGiants: return form(0x30529);                               // giant
            default: return nullptr;
        }
    }

    RE::TESBoundObject* ArcBridge::GangBase(std::uint8_t kind, std::uint32_t seed) {
        auto* data = RE::TESDataHandler::GetSingleton();
        const auto form = [&](RE::FormID id) -> RE::TESBoundObject* {
            auto* f = data ? data->LookupForm(id, "Skyrim.esm") : nullptr;
            return f ? f->As<RE::TESBoundObject>() : nullptr;
        };
        switch (kind) {
            case Tales::SiteDwemer: return form(seed % 2u ? 0x10EC90 : 0x10EC8F);       // Dwarven spider, sphere
            case Tales::SiteFalmer: return form(0x1E786);
            case Tales::SiteCrypt: return Papyrus::LookupLeveledActor("LvlDraugrMeleeAllMale");
            case Tales::SiteBandits: return Papyrus::LookupLeveledActor(seed % 2u ? "LvlBanditMissile" : "LvlBanditMeleeAny");
            case Tales::SiteDen: return form(0x41FB6);                                  // frostbite spiders
            case Tales::SiteVampires: return form(0x33973);
            case Tales::SiteNecromancers: return form(0x1091C9);
            default: return nullptr;                                                    // they keep no company
        }
    }

    void ArcBridge::BossPlaced(std::uint32_t id, int site, RE::Actor* boss) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || site < 0 || site >= static_cast<int>(A::kSites)) return;
        const auto s = static_cast<std::size_t>(site);
        a->siteState[s] |= 1u;
        a->siteBoss[s] = boss ? boss->GetFormID() : 0u;
        logger::info("Arcs: #{} {} waits at the heart of {} ({:08X})", id, a->bossName[s].empty() ? std::string("its master") : a->bossName[s],
                     NameOf(a->site[s]), a->siteBoss[s]);
    }

    RE::TESBoundObject* ArcBridge::RemainsBase(std::uint8_t what) {
        auto* data = RE::TESDataHandler::GetSingleton();
        const auto form = [&](RE::FormID id) -> RE::TESBoundObject* {
            auto* f = data ? data->LookupForm(id, "Skyrim.esm") : nullptr;
            return f ? f->As<RE::TESBoundObject>() : nullptr;
        };
        switch (what) {
            case A::RemainsHunter: return form(0x73FBF);   // EncHunterNordM: a man, so the writer's "he" is true
            case A::RemainsCow: return form(0x23A90);      // EncCow
            case A::RemainsHorse: return form(0x68D5B);    // EncHorseBrown
            case A::RemainsGoat: return form(0x4359C);     // EncGoatDomestic
            case A::RemainsDeer: return form(0xCF89D);     // EncDeer
            default: return nullptr;
        }
    }

    RE::TESForm* ArcBridge::RemedyFor(std::uint8_t kind, bool spider) {
        auto* data = RE::TESDataHandler::GetSingleton();
        const auto form = [&](RE::FormID id) -> RE::TESForm* { return data ? data->LookupForm(id, "Skyrim.esm") : nullptr; };
        switch (kind) {
            case Tales::SiteDwemer: return form(0x39B4A);                          // Resist Fire: a centurion's steam
            case Tales::SiteFalmer: return form(0x65A64);                          // Cure Poison: their arrows
            case Tales::SiteCrypt: return form(0x39B8A);                           // Resist Frost: an overlord's voice
            case Tales::SiteBandits: return form(0x3EADF);                         // Healing: plain steel
            case Tales::SiteDen: return form(spider ? 0x65A64 : 0x96598);          // Cure Poison; a scroll of Firebolt (a troll burns)
            case Tales::SiteVampires: return form(0xAE723);                        // Cure Disease: what their bite carries
            case Tales::SiteNecromancers: return form(0x39E52);                    // Resist Magic
            case Tales::SiteForsworn: return form(0x39E52);                        // Resist Magic: the shaman's spells
            case Tales::SiteHagraven: return form(0x39B4A);                        // Resist Fire: her fire
            case Tales::SiteGiants: return form(0x3EADF);                          // Healing: nothing else helps
            default: return nullptr;
        }
    }

    void ArcBridge::CluePlaced(std::uint32_t id, int clue, RE::TESObjectREFR* lies) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || clue < 0 || clue >= static_cast<int>(A::kClues) || !lies) return;
        const auto k = static_cast<std::size_t>(clue);
        if (!a->clueWhat[k] || a->clueState[k] != A::ClueToPlace) return;
        a->clueRef[k] = lies->GetFormID();
        a->clueState[k] = A::CluePlaced;
        logger::info("Arcs: #{} what was left of a victim lies outside {} ({:08X})", id, NameOf(a->site[a->clueSite[k] - 1u]), a->clueRef[k]);
        RefreshBound(m_book);
    }

    void ArcBridge::SpiritOut(std::uint32_t id, RE::Actor* shade) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || !shade || !a->spiritSite || a->spiritState != A::SpiritAway) return;
        a->spiritRef = shade->GetFormID();
        a->spiritState = A::SpiritOut;
        SetSpirit(a->id, a->spiritRef, false);
        logger::info("Arcs: #{} {} walks outside {} ({:08X}); it is night", id, a->spiritName, NameOf(a->site[a->spiritSite - 1u]), a->spiritRef);
    }

    void ArcBridge::SpiritGone(std::uint32_t id, int how) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || !a->spiritSite) return;
        if (how == 0 && a->spiritState == A::SpiritOut) {
            a->spiritState = A::SpiritAway;
            logger::info("Arcs: #{} {} fades with the light: it walks again the next night", id, a->spiritName);
        }
        a->spiritRef = 0;
        SetSpirit(a->id, 0, false);
    }

    bool ArcBridge::SpiritRest(std::uint32_t id) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || !player || !A::IsOpen(*a) || a->spiritState != A::SpiritOut || !a->spiritWants) return false;
        const auto p = static_cast<std::size_t>(a->spiritWants - 1u);
        const auto found = FindStamped(player, a->propBase[p], a->propName[p]);
        if (!found.object) {
            logger::info("Arcs: #{} {} cannot be laid to rest: '{}' is not on the player", id, a->spiritName, a->propName[p]);
            return false;
        }
        // It goes with the dead: out of the player's pack and out of the world.
        player->RemoveItem(found.object, 1, RE::ITEM_REMOVE_REASON::kRemove, found.extra, nullptr);
        a->spiritState = A::SpiritRested;
        SetSpirit(a->id, 0, false);
        const auto nodes = A::NodesOf(*a);
        const bool asked = a->node < nodes.size() && nodes[a->node].kind == A::Kind::Haunt;
        logger::info("Arcs: #{} {} is laid to rest: the player brought '{}'{}", id, a->spiritName, a->propName[p],
                     asked ? "" : " (before the story asked for it)");
        return true;
    }

    void ArcBridge::Captive(std::uint32_t id, int state) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || !a->captive || state < 0 || state > A::CaptiveHome) return;
        // Free they become by the story's step alone (Advance); the script tells the rest.
        if (state == A::CaptiveFreed || a->captiveState == state) return;
        static constexpr const char* kStates[] = {"is to be taken", "is away, taken", "is bound at the heart of", "is free", "has gone their own way from"};
        logger::info("Arcs: #{} {} {} {}", id, NameOf(a->cast[a->captive]), kStates[state], a->captiveSite ? NameOf(a->site[a->captiveSite - 1u]) : std::string());
        a->captiveState = static_cast<std::uint8_t>(state);
        RefreshBound(m_book);
    }

    void ArcBridge::AmbushLook(std::uint32_t id, A::Arc& a, Pending& pending, const A::Node& n, float now, int placed) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const float real = RealSeconds();
        if (pending.ambushDo) return;   // the script has work to do first
        const bool wilds = InTheWilds(player);
        if (!wilds) pending.wildsSince = -1.0f;
        else if (pending.wildsSince < 0.0f) pending.wildsSince = real;
        if (!placed) {
            // Out in the wilds for a while, a quarter of an hour of the game into the step, nothing else going on, and
            // no quest of the game moving lately (the player is not to be stopped in the middle of the main story).
            const float hours = (now - a.since) * 24.0f;
            std::string wait;
            if (!wilds) {
                auto* loc = player->GetCurrentLocation();
                const char* where = loc ? loc->GetFullName() : nullptr;
                wait = std::string("not out in the wilds (") + (where && *where ? where : "an interior or nowhere named") + ")";
            } else if (real - pending.wildsSince < 20.0f || hours < 0.25f) {
                wait = "too soon";
            } else if (real < pending.ambushHold || now < pending.ambushAgain) {
                wait = "after the last try";
            } else if (player->IsInCombat() || LivingWorldBridge::GetSingleton()->IsBadMoment()) {
                wait = "a fight or a menu";
            } else if (real < m_questBusyUntil.load()) {
                wait = "a quest of the game moved on lately";
            }
            if (wait != pending.ambushWait) {
                pending.ambushWait = wait;
                if (!wait.empty()) logger::info("Arcs: #{} the ambush waits: {}", id, wait);
            }
            if (!wait.empty()) return;
            pending.ambushDo = 1;
            pending.ambushHold = real + 30.0f;
            logger::info("Arcs: #{} the ambush: its men are to wait on the player's way", id);
            return;
        }
        auto* leader = a.thug[0] ? RE::TESForm::LookupByID<RE::Actor>(a.thug[0]) : nullptr;
        bool anyLoaded = false, anyAlive = false, fighting = false;
        float nearest = 1.0e9f;
        for (const auto ref : a.thug) {
            auto* t = ref ? RE::TESForm::LookupByID<RE::Actor>(ref) : nullptr;
            if (!t || t->IsDead() || t->IsDeleted()) continue;
            anyAlive = true;
            if (!t->Is3DLoaded()) continue;
            anyLoaded = true;
            // Fighting the player (or someone at the player's side), not a wolf that crossed their path.
            if (t->IsInCombat()) {
                auto target = t->GetActorRuntimeData().currentCombatTarget.get();
                fighting = fighting || (target && (target.get() == player || target->IsPlayerTeammate()));
            }
            if (t->GetWorldspace() == player->GetWorldspace()) nearest = std::min(nearest, t->GetPosition().GetDistance(player->GetPosition()));
        }
        if (!anyAlive) {
            // None of them alive and none fell: the game took them away (a cell reset). They come again later.
            const bool anyDead = std::any_of(a.thug.begin(), a.thug.end(), [](std::uint32_t ref) {
                auto* t = ref ? RE::TESForm::LookupByID<RE::Actor>(ref) : nullptr;
                return t && !t->IsDeleted() && t->IsDead();
            });
            if (!anyDead) {
                for (std::size_t p = 0; p < A::kProps; ++p) {
                    if ((a.propAmbush & (1u << p)) && std::find(a.thug.begin(), a.thug.end(), a.propAt[p]) != a.thug.end()) a.propAt[p] = 0;
                }
                a.thug = {};
                pending.struck = false;
                pending.parleyAt = -1.0f;
                logger::info("Arcs: #{} the ambush: its men are gone from the game unfought; they come again later", id);
            }
            return;
        }
        if (pending.placedAt < 0.0f) pending.placedAt = real;
        {
            const std::string state = std::string(anyLoaded ? "loaded" : "not loaded") + (fighting ? ", fighting the player" : "") +
                                      (player->IsInCombat() ? ", the player in a fight" : "") + (nearest > 12000.0f ? ", far" : "") +
                                      (pending.struck ? ", the fight is on" : pending.parleyAt >= 0.0f ? ", the leader has the word" : "");
            if (state != pending.ambushWait) {
                pending.ambushWait = state;
                logger::info("Arcs: #{} the ambush's men: {}", id, state);
            }
        }
        if (pending.struck) {
            // The fight is on: whenever they are not on the player (they lost the player, or something drew them off:
            // Brannoc and his man ran after a wolf into the river and never came back, 2026-11-19) they come again.
            if (fighting) return;
            if (anyLoaded && wilds && nearest < 2000.0f && real >= pending.strikeHold) {
                pending.ambushDo = 5;
                pending.strikeHold = real + 15.0f;
                logger::info("Arcs: #{} the ambush: the player is {:.0f} away, they come at the player again", id, nearest);
                return;
            }
            if (anyLoaded && wilds && nearest >= 2000.0f && real >= pending.comeHold) {
                pending.ambushDo = 4;
                pending.comeHold = real + 15.0f;
                logger::info("Arcs: #{} the ambush: they are not on the player any more ({:.0f} away); they come after the player", id, nearest);
                return;
            }
        } else if (fighting) {
            // The player struck first (or they turned on the player some other way): all of them join, no word.
            pending.ambushDo = 5;
            pending.strikeHold = real + 15.0f;
            logger::info("Arcs: #{} the ambush: the player is {:.0f} away, they join the fight", id, nearest);
            return;
        } else if (pending.parleyAt >= 0.0f) {
            // The leader has the word; the player may answer, and the talk may settle it.
            std::string why;
            if (pending.parleyFight) why = "the player cannot pay what was offered";
            if (pending.parleyFight || Parley(id, a, pending, n, nearest, why)) {
                pending.ambushDo = 5;
                pending.strikeHold = real + 15.0f;
                logger::info("Arcs: #{} the ambush: {}; they fall on the player", id, why);
            }
            return;
        } else {
            // Not met yet. Near: the leader steps out (or closes in) and has a word, out in the wilds only.
            if (anyLoaded && wilds && nearest < 2000.0f) {
                pending.ambushDo = 2;
                logger::info("Arcs: #{} the ambush: the player is {:.0f} away, they {}", id, nearest, pending.chasing ? "close in" : "step out");
                return;
            }
            // Waiting on the road and the player has not come their way for a minute: they come to the player, so the
            // meeting is where the player is (the user, 2026-11-19: the ambusher stood somewhere in the woods where
            // nobody passed).
            if (anyLoaded && wilds && !pending.chasing && real - pending.placedAt > 60.0f && nearest > 2500.0f && real >= pending.comeHold) {
                pending.ambushDo = 4;
                pending.comeHold = real + 15.0f;
                logger::info("Arcs: #{} the ambush: the player did not come their way ({:.0f} away); they come after the player", id, nearest);
                return;
            }
        }
        // The player got away: out of the fight and far from them (this modlist keeps them loaded 17000 units off,
        // 2026-11-19), or indoors, or in a town (they do not follow the player in among guards and townsfolk). They
        // are taken out of the world unseen and come again another time.
        auto* cell = player->GetParentCell();
        const bool away = !fighting && !player->IsInCombat() && real >= pending.callOffHold &&
                          (!cell || cell->IsInteriorCell() || nearest > 12000.0f || (!wilds && !pending.struck));
        if (away) {
            pending.ambushDo = 3;
            pending.callOffHold = real + 10.0f;
            logger::info("Arcs: #{} the ambush: the player {} {}; they are called off and come again later", id,
                         !wilds && cell && !cell->IsInteriorCell() ? "reached where people live, away from" : "got away from",
                         leader ? NameOf(leader->GetFormID()) : std::string("its men"));
        }
    }

    void ArcBridge::ThugPlaced(std::uint32_t id, int k, RE::Actor* thug) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || k < 0 || k >= static_cast<int>(A::kThugs) || !thug) return;
        a->thug[static_cast<std::size_t>(k)] = thug->GetFormID();
        logger::info("Arcs: #{} the ambush: {} ({:08X}) waits{}", id, thug->GetDisplayFullName(), thug->GetFormID(), k == 0 ? ", leading them" : "");
    }

    void ArcBridge::AmbushDone(std::uint32_t id, int what) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        auto it = m_pending.find(id);
        if (!a || it == m_pending.end()) return;
        auto& pending = it->second;
        pending.ambushDo = 0;
        if (what == 1) {
            const bool any = std::any_of(a->thug.begin(), a->thug.end(), [](std::uint32_t t) { return t != 0; });
            if (!any) {
                ++pending.ambushTries;
                logger::info("Arcs: #{} the ambush: no stretch of road out of sight on the player's way yet; tried again in a while", id);
            } else {
                pending.placedAt = RealSeconds();
                pending.chasing = false;
            }
        } else if (what == 2) {
            // The leader stepped out and has the word: the player may answer before anything else happens.
            pending.parleyAt = RealSeconds();
            pending.judgedAt = pending.parleyAt;
            pending.askedAt = -1.0f;
            pending.nones = 0;
            pending.talked = false;
            pending.judging = false;
            pending.turn = -1;
            pending.parleyFight = false;
        } else if (what == 5) {
            pending.struck = true;
            pending.parleyAt = -1.0f;
            pending.parleyFight = false;
            pending.shortPurse = false;
        } else if (what == 4) {
            pending.chasing = true;
        } else if (what == 3) {
            // The living ones the script took out unseen are forgotten; the dead stay where they fell (and count); one
            // still in the player's sight stays until it can go unseen (asked again).
            bool left = false;
            for (std::size_t k = 0; k < A::kThugs; ++k) {
                auto* t = a->thug[k] ? RE::TESForm::LookupByID<RE::Actor>(a->thug[k]) : nullptr;
                if (t && t->IsDead()) continue;
                if (t && !t->IsDeleted() && !t->IsDisabled()) {
                    left = true;
                    continue;
                }
                for (std::size_t p = 0; p < A::kProps; ++p) {
                    if ((a->propAmbush & (1u << p)) && a->propAt[p] == a->thug[k]) a->propAt[p] = 0;   // it went with them
                }
                a->thug[k] = 0;
            }
            pending.chasing = false;
            if (!left) {
                pending.parleyAt = -1.0f;
                pending.struck = false;
                pending.ambushAgain = NowDays() + 0.25f;   // six hours of the game
            }
        }
    }

    void ArcBridge::GangPlaced(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        if (auto it = m_pending.find(id); it != m_pending.end()) it->second.gangSite = 0;
    }

    bool ArcBridge::SiteFree(RE::BGSLocation* location, const std::unordered_set<RE::FormID>& objectiveLocations, std::string& why) const {
        if (!location) return why = "no place", false;
        if (objectiveLocations.contains(location->GetFormID())) return why = "a quest of the player's leads there", false;
        if (const auto it = m_questPlaces.find(location->GetFormID()); it != m_questPlaces.end()) {
            for (const auto questId : it->second) {
                auto* quest = RE::TESForm::LookupByID<RE::TESQuest>(questId);
                if (quest && !quest->IsCompleted()) {
                    const char* name = quest->GetFullName();
                    return why = std::string("a quest of the game still needs it (") + (name && *name ? name : "a quest") + ")", false;
                }
            }
        }
        if (QuestBridge::GetSingleton()->PlaceInUse(location->GetFormID())) return why = "a quest of IntelEngine's is set there", false;
        std::scoped_lock lock(m_mutex);
        for (const auto& a : m_book.all) {
            if (!A::IsLive(a)) continue;
            for (const auto s : a.site) {
                if (s == location->GetFormID()) return why = "another story is set there", false;
            }
        }
        return true;
    }

    bool ArcBridge::FetchProp(std::uint32_t id, int prop) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || !player || prop < 0 || prop >= static_cast<int>(A::kProps) || !a->propBase[prop]) return false;
        const auto p = static_cast<std::size_t>(prop);
        if (FindStamped(player, a->propBase[p], a->propName[p]).object) return true;   // the player has it already
        auto* at = a->propAt[p] ? RE::TESForm::LookupByID<RE::TESObjectREFR>(a->propAt[p]) : nullptr;
        if (!at) return false;
        const auto found = FindStamped(at, a->propBase[p], a->propName[p]);
        if (!found.object) {
            logger::info("Arcs: #{} '{}' is no longer where it was put", id, a->propName[p]);
            return false;
        }
        at->RemoveItem(found.object, 1, RE::ITEM_REMOVE_REASON::kStoreInContainer, found.extra, player);
        a->propAt[p] = player->GetFormID();
        logger::info("Arcs: #{} '{}' handed to the player from {:08X}", id, a->propName[p], at->GetFormID());
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
        const auto nodes = A::NodesOf(*a);
        const auto pending = m_pending.find(id);
        if (name == "phase") return static_cast<int>(a->phase);
        if (name == "tale") return a->pattern == A::Pattern::Tale ? 1 : 0;
        if (name == "price") return static_cast<int>(a->price ? a->price : a->gold);
        if (name == "bondwith") return pending != m_pending.end() ? pending->second.bondWith : 0;
        if (name == "effectprop") return pending != m_pending.end() ? pending->second.effectProp : 0;
        if (name == "gangsite") return pending != m_pending.end() ? pending->second.gangSite - 1 : -1;
        if (name == "camefar") return (a->flags & A::Flag::CameFar) ? 1 : 0;
        if (name == "captivestate") return a->captive ? static_cast<int>(a->captiveState) : -1;
        if (name == "captiveout") {
            // The player has left the place someone was held at, and stands under the open sky: they part here.
            if (!a->captive || !a->captiveSite) return 1;
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* cell = player ? player->GetParentCell() : nullptr;
            if (!cell || cell->IsInteriorCell()) return 0;
            auto* site = RE::TESForm::LookupByID<RE::BGSLocation>(a->site[a->captiveSite - 1u]);
            for (auto* at = player->GetCurrentLocation(); at; at = at->parentLoc) {
                if (at == site) return 0;
            }
            return 1;
        }
        if (name == "ambushdo") return pending != m_pending.end() ? pending->second.ambushDo : 0;
        if (name == "ambushtries") return pending != m_pending.end() ? pending->second.ambushTries : 0;
        if (name == "ambushchase") return pending != m_pending.end() && pending->second.chasing ? 1 : 0;
        if (name == "ambushend") return pending != m_pending.end() ? pending->second.standWhy : 0;
        if (name == "ambushshort") return pending != m_pending.end() && pending->second.shortPurse ? 1 : 0;
        if (name == "ambushlost") {
            // The story has a way on for a beaten player: only then is the player kept from dying in it.
            for (const auto& step : nodes) {
                if (step.kind != A::Kind::Ambush) continue;
                for (const auto& e : step.edges) if (e.when == A::If::Lost) return 1;
            }
            return 0;
        }
        if (name == "struck") return pending != m_pending.end() && pending->second.struck ? 1 : 0;
        if (name == "thugs") {
            // How many come: fitted to the player.
            auto* player = RE::PlayerCharacter::GetSingleton();
            // One alone for someone who could not stand against two (the game's Hired Thugs start at level 6: two of
            // them killed a level 1 test character in twenty seconds, 2026-11-19).
            const int level = player ? player->GetLevel() : 1;
            return level >= 12 ? 3 : level >= 6 ? 2 : 1;
        }
        if (name == "quiet") {
            // Seconds since the step's person or the player last spoke (the talk between them going quiet).
            if (a->node >= nodes.size() || !nodes[a->node].who) return 9999;
            auto* speech = SpeechTracker::GetSingleton();
            const RE::FormID who = a->cast[nodes[a->node].who];
            if (speech->IsSpeechPending(who)) return 0;
            const float theirs = speech->SecondsSinceSpeechEnded(who);
            const float players = speech->SecondsSincePlayerSpoke();
            float quiet = 9999.0f;
            if (theirs >= 0.0f) quiet = std::min(quiet, theirs);
            if (players >= 0.0f) quiet = std::min(quiet, players);
            return static_cast<int>(quiet);
        }
        if (name == "pattern") return static_cast<int>(a->pattern);
        if (name == "slot") return a->slot == 0xFF ? -1 : a->slot;
        if (name == "node") return a->node;
        if (name == "nodes") return static_cast<int>(nodes.size());
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
        if (name == "kind") return a->node < nodes.size() && A::IsOpen(*a) ? static_cast<int>(nodes[a->node].kind) : 0;
        if (name == "objective") {
            return a->node < nodes.size() && A::IsOpen(*a) ? nodes[a->node].objective : -1;
        }
        if (name.starts_with("objective")) {
            const auto n = static_cast<std::size_t>(std::atoi(name.c_str() + 9));
            return n < nodes.size() ? nodes[n].objective : -1;
        }
        if (name == "options") {
            if (a->node >= nodes.size()) return 0;
            int count = 0;
            for (const auto& o : nodes[a->node].options) count += o.empty() ? 0 : 1;
            return count;
        }
        return -1;
    }

    RE::TESForm* ArcBridge::Form(std::uint32_t id, const std::string& name) const {
        std::scoped_lock lock(m_mutex);
        const auto* a = m_book.Find(id);
        if (!a) return nullptr;
        const auto nodes = A::NodesOf(*a);
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
        if (name == "captive") return a->captive ? actor(a->captive) : nullptr;
        if (name == "captiveanchor") {
            return a->captive && a->captiveSite && a->siteAnchor[a->captiveSite - 1u] ? RE::TESForm::LookupByID(a->siteAnchor[a->captiveSite - 1u]) : nullptr;
        }
        if (name.starts_with("remedy")) {
            // What helps against what lives in place <s> of the story: given by whoever is told rightly what did it.
            const auto s = static_cast<std::size_t>(std::atoi(name.c_str() + 6));
            if (s >= A::kSites || !a->site[s]) return nullptr;
            return RemedyFor(a->siteKind[s], A::detail::Mix(a->seed, a->site[s]) % 2u != 0u);
        }
        if (name.starts_with("spirit")) {
            // What walks by night: "spiritbase" whose shade to put there (only while it is night, it is not out and
            // the story still wants it), "spiritanchor" the ground it walks, "spirit" itself.
            if (!a->spiritSite) return nullptr;
            if (name == "spiritanchor") return OutsideOf(*a, a->spiritSite);
            if (name == "spiritbase") {
                if (a->spiritState != A::SpiritAway || !SpiritWanted(*a) || !A::IsNight(HourNow())) return nullptr;
                return RE::TESForm::LookupByID(a->spiritWoman ? kShadeWoman : kShadeMan);
            }
            return a->spiritRef ? RE::TESForm::LookupByID(a->spiritRef) : nullptr;
        }
        if (name.starts_with("clue")) {
            // What is to be looked at outside a place: "cluebase<k>" who or what to lay there (only while it does not
            // lie there and the story still wants it), "clueanchor<k>" the ground it lies by, "clue<k>" itself.
            const bool base = name.starts_with("cluebase"), anchor = name.starts_with("clueanchor");
            const auto k = static_cast<std::size_t>(std::atoi(name.c_str() + (base ? 8 : anchor ? 10 : 4)));
            if (k >= A::kClues || !a->clueWhat[k]) return nullptr;
            if (anchor) return OutsideOf(*a, a->clueSite[k]);
            if (base) return a->clueState[k] == A::ClueToPlace && ClueWanted(*a, k) ? RemainsBase(a->clueWhat[k]) : nullptr;
            return a->clueRef[k] ? RE::TESForm::LookupByID(a->clueRef[k]) : nullptr;
        }
        if (name.starts_with("bossbase") || name.starts_with("siteanchor") || name.starts_with("gangbase")) {
            // The master a place of the story needs (its own kind), and where its heart is; "gangbase<k>" the company
            // of the place a look asked for (PlaceGang).
            const auto pending = m_pending.find(id);
            std::size_t s = static_cast<std::size_t>(std::atoi(name.c_str() + (name.starts_with("gangbase") ? 8 : name.starts_with("bossbase") ? 8 : 10)));
            if (name.starts_with("gangbase")) {
                if (pending == m_pending.end() || !pending->second.gangSite) return nullptr;
                return GangBase(a->siteKind[pending->second.gangSite - 1u], static_cast<std::uint32_t>(s) + a->seed);
            }
            if (s >= A::kSites || !a->site[s]) return nullptr;
            if (name.starts_with("siteanchor")) return a->siteAnchor[s] ? RE::TESForm::LookupByID(a->siteAnchor[s]) : nullptr;
            if (a->siteState[s] & 1u) return nullptr;   // placed already
            bool needed = false;
            for (const auto& n : a->graph) needed = needed || (n.kind == A::Kind::Slay && n.site == s + 1);
            for (const auto p : a->propSite) needed = needed || p == s + 1;
            // Which of a kind's masters (a den's spider or troll) follows from the story and the place alone, so that
            // the writer could be told before a word was written (TaleAuthor: the place's "heart" and "signs").
            return needed ? BossBase(a->siteKind[s], A::detail::Mix(a->seed, a->site[s])) : nullptr;
        }
        if (name == "thugsender") {
            for (const auto& step : nodes) {
                if (step.kind == A::Kind::Ambush) return actor(step.who ? step.who : static_cast<std::size_t>(A::RAsker));
            }
            return nullptr;
        }
        if (name.starts_with("thugbase")) {
            // The game's own Hired Thugs (Skyrim.esm WIThug, WIThugNordM, WIThugCommonerM): leveled to the player,
            // named by the game in its own language.
            static constexpr RE::FormID kThugBases[] = {0x35B5E, 0xD783B, 0xD783C};
            const auto k = static_cast<std::uint32_t>(std::atoi(name.c_str() + 8));
            return RE::TESForm::LookupByID(kThugBases[(a->seed + k) % 3u]);
        }
        if (name.starts_with("thug")) {
            const auto k = static_cast<std::size_t>(std::atoi(name.c_str() + 4));
            return k < A::kThugs && a->thug[k] ? RE::TESForm::LookupByID(a->thug[k]) : nullptr;
        }
        if (name.starts_with("pocket")) {
            // Who carries a prop from the start: a tale's pocket, the murder's gem with the killer.
            const auto p = static_cast<std::size_t>(std::atoi(name.c_str() + 6));
            if (p >= A::kProps || !a->propBase[p]) return nullptr;
            if (a->pattern == A::Pattern::Tale) return a->propPocket[p] ? actor(a->propPocket[p]) : nullptr;
            return a->pattern == A::Pattern::Murder && p == 1 ? actor(A::RCulprit) : nullptr;
        }
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
        if (a->node >= nodes.size() || !A::IsOpen(*a)) return nullptr;
        const auto& n = nodes[a->node];
        if (name == "who") return actor(n.who);
        if (name == "giveto") {
            A::Role to = A::RNone;
            std::uint8_t prop = 0;
            return HandOverOf(n, to, prop) ? actor(to) : nullptr;
        }
        if (name == "marker") return MarkerOf(*a, n);   // one answer for the journal, asked anew on every watch
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
        // A tale's lines name its people by role number where the engine wrote them ("{2} is dead.").
        for (std::size_t r = 1; r < A::kRoles; ++r) {
            const std::string key = "{" + std::to_string(r) + "}";
            if (text.find(key) != std::string::npos) text = A::Fill(std::move(text), key, NameOf(a.cast[r]));
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
        if (name == "story") return Fill(a->story, *a);
        if (name.starts_with("bossname")) {
            const auto s = static_cast<std::size_t>(std::atoi(name.c_str() + 8));
            return s < A::kSites ? a->bossName[s] : std::string();
        }
        if (name == "thugname") return a->thugName;
        if (name == "captiveplace") return a->captive && a->captiveSite ? NameOf(a->site[a->captiveSite - 1u]) : std::string();
        if (name == "spiritname") return a->spiritName;
        if (name == "spiritplace") return a->spiritSite ? NameOf(a->site[a->spiritSite - 1u]) : std::string();
        if (name == "spiritwants") return a->spiritWants ? a->propName[a->spiritWants - 1u] : std::string();
        if (name.starts_with("cluename")) {
            // The name the story gives the dead one ("Torvald"), for the body to carry; empty when it gives none.
            const auto k = static_cast<std::size_t>(std::atoi(name.c_str() + 8));
            for (const auto& step : A::NodesOf(*a)) {
                if (step.kind == A::Kind::Examine && step.prop == k) return step.why;
            }
            return {};
        }
        if (name.starts_with("cluetext")) {
            // What the player sees on what was left there (the writer's words, from the place's own signs).
            const auto k = static_cast<std::size_t>(std::atoi(name.c_str() + 8));
            for (const auto& step : A::NodesOf(*a)) {
                if (step.kind == A::Kind::Examine && step.prop == k) return Fill(step.question, *a);
            }
            return {};
        }
        if (name == "thugwhy") {
            // What the men know of their errand (a fact in their bios): who paid them, and for what.
            for (const auto& n : A::NodesOf(*a)) {
                if (n.kind != A::Kind::Ambush) continue;
                const std::string by = n.who ? NameOf(a->cast[n.who]) + "'s coin" : std::string("coin");
                std::string fact = "took " + by + " to waylay " + PlayerName() + " on the road: " + Fill(n.why, *a);
                // What they would settle for, so the leader can be talked to.
                std::string settle;
                for (const auto& turn : n.turns) {
                    if (!turn.empty()) settle += (settle.empty() ? "" : "; or: ") + Fill(turn, *a);
                }
                if (!settle.empty()) fact += ". It need not come to blood if: " + settle;
                return fact;
            }
            return {};
        }
        if (name == "closing") return a->phase == A::Phase::Ended ? Fill(A::TaleClosing(*a), *a) : std::string();
        const auto nodes = A::NodesOf(*a);
        if (a->node >= nodes.size()) return {};
        const auto& n = nodes[a->node];
        if (name == "who") return NameOf(a->cast[n.who]);
        if (name == "goal") return Fill(n.goal, *a);
        if (name == "visitwords") {
            // What brings the step's person to the player, from their own side (the author's "why"); else their part
            // in the matter, which their own lines tell them.
            const std::string who = "{" + std::to_string(static_cast<int>(n.who)) + "}";
            if (!n.why.empty()) return Fill(n.why, *a);
            return Fill(who + " seeks {player} out to talk about the matter of \"" + a->title + "\", as " + who +
                            " sees it", *a);
        }
        if (name.starts_with("option")) {
            const auto i = static_cast<std::size_t>(std::atoi(name.c_str() + 6));
            if (i >= 3 || n.options[i].empty()) return {};
            // An answer that costs the player gold is not offered when they do not have it.
            for (const auto& e : n.edges) {
                const bool thisAnswer = e.when == static_cast<A::If>(static_cast<int>(A::If::Option0) + static_cast<int>(i));
                if (thisAnswer && e.effect == A::Effect::PlayerPays) {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0xF);
                    const auto cost = a->price ? a->price : a->gold;
                    if (!player || !gold || player->GetItemCount(gold) < static_cast<std::int32_t>(cost)) return {};
                }
            }
            return Fill(n.options[i], *a);
        }
        if (name == "fight") return Fill("Settle it with {rival} here and now, with your fists?", *a);
        if (name == "grievance") return a->pattern == A::Pattern::Feud ? Fill(A::FeudGrievance(a->seed).heard, *a) : std::string();
        if (name == "question") {
            if (!n.question.empty()) return Fill(n.question, *a);
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
            // "back" only when it is theirs (named after them): Sven's garnet goes TO Faendal (2026-11-19).
            const std::string name = NameOf(a->cast[to]);
            const std::string first = name.substr(0, name.find(' '));
            const bool theirs = !first.empty() && a->propName[prop].find(first) != std::string::npos;
            return Fill("Give {item" + std::to_string(prop) + (theirs ? "} back to " : "} to ") + name + "?", *a);
        }
        return {};
    }

    // ------------------------------------------------------------------ what people know

    void ArcBridge::OfferDeferred(std::uint32_t id, const std::string& company) {
        std::scoped_lock lock(m_mutex);
        auto* a = m_book.Find(id);
        if (!a || a->phase != A::Phase::Planned) return;
        auto& pending = m_pending[id];
        pending.offerSent = false;
        a->deadline = std::max(a->deadline, NowDays() + 3.0f);   // it keeps for a few days
        if (a->flags & A::Flag::CameFar) {
            // Someone who came from another hold is not at home here: they try again within the hour.
            pending.offerDue = NowDays() + 1.0f / 24.0f;
        } else {
            a->flags |= A::Flag::Deferred;
            pending.deferredAt = RealSeconds();
        }
        if (company.empty()) {
            logger::info("Arcs: #{} {} could not reach the player (gone): the story waits until they next meet", id, NameOf(a->cast[A::RAsker]));
        } else {
            logger::info("Arcs: #{} {} holds back: {} stands with the player, and the story is about them too", id, NameOf(a->cast[A::RAsker]), company);
        }
    }

    bool ArcBridge::Hurries(std::uint32_t id, RE::Actor* actor) const {
        if (!actor) return false;
        if (LivingWorldBridge::GetSingleton()->CloseToPlayer(actor)) return true;
        std::scoped_lock lock(m_mutex);
        const auto* a = id ? m_book.Find(id) : nullptr;
        // Having failed to reach the player once is no reason to run the next time: Faendal, a stranger with a small
        // matter, jogged at the player on their second meeting (the user, 2026-11-19: "He Faendal still came running
        // though"). Only who came all the way from another hold has truly been looking.
        return a && a->cast[A::RAsker] == actor->GetFormID() && (a->flags & A::Flag::CameFar) != 0;
    }

    int ArcBridge::KnowsPlayer(RE::Actor* actor, RE::FormID town) const {
        if (!actor) return 0;
        const RE::FormID id = actor->GetFormID();
        if (MemoryDB::GetSingleton()->HasMetPlayer(id) || DialogueTracker::GetSingleton()->GetCount(id) > 0 ||
            LivingWorldBridge::GetSingleton()->CloseToPlayer(actor)) {
            return 2;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* npc = actor->GetActorBase();
        auto* pc = player ? player->GetActorBase() : nullptr;
        if (npc && pc && RE::BGSRelationship::GetRelationship(npc, pc)) return 2;   // the game itself ties them
        // What the player did in their town went round it: an earlier story there that ended and was talked about.
        std::scoped_lock lock(m_mutex);
        const bool heard = town && std::any_of(m_book.all.begin(), m_book.all.end(), [&](const A::Arc& a) {
            return a.place == town && a.phase == A::Phase::Ended && a.rumor != 0;
        });
        return heard ? 1 : 0;
    }

    void ArcBridge::OfferLater(std::uint32_t id) {
        std::scoped_lock lock(m_mutex);
        auto& pending = m_pending[id];
        pending.offerSent = false;
        pending.offerDue = NowDays() + 1.0f / 24.0f;
        logger::info("Arcs: #{} the offer could not go out now: tried again in an hour", id);
    }

    bool ArcBridge::FarComerLately(float days) const {
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        return std::any_of(m_book.all.begin(), m_book.all.end(), [&](const A::Arc& a) {
            return (a.flags & A::Flag::CameFar) && a.ending != A::Ending::Lapsed && now - a.created < days && now >= a.created;
        });
    }

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
            // What walks by night is no one of the town: it knows what the story says it knows, and what it is.
            if (A::IsOpen(a) && a.spiritRef == id && a.spiritState == A::SpiritOut && a.spiritSite) {
                const std::string place = NameOf(a.site[a.spiritSite - 1u]);
                std::string line = "I am not among the living. I am " + a.spiritName + ", a shade that walks outside " + place +
                                   " between nightfall and first light, seen and heard by whoever comes here. " + a.spiritTruth +
                                   " I speak as the dead might: little, plainly, of what I remember and what I want. I harm nobody who leaves me "
                                   "be. I cannot leave this ground, no living hand can touch me, and I fade when the sky grows light.";
                if (a.spiritWants) {
                    line += " What would let me rest at last: " + a.propName[a.spiritWants - 1u] +
                            ", brought here and held out to me. I cannot fetch it, and I do not know where it is now.";
                }
                out += "- " + Fill(line, a) + "\n";
                continue;
            }
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
            auto* quest = JournalQuest(a.slot);
            if (!quest) continue;
            quest->fullName = Fill(a.title, a).c_str();
            if (a.pattern != A::Pattern::Tale) continue;
            // A tale's step goals are its objectives' texts (objectives kTaleObjective.. of the slot's quest).
            for (auto* objective : quest->objectives) {
                if (!objective || objective->index < A::kTaleObjective) continue;
                const auto n = static_cast<std::size_t>(objective->index - A::kTaleObjective);
                if (n < a.graph.size()) objective->displayText = Fill(a.graph[n].goal, a).c_str();
            }
            // The log: the story so far, and how it ended, as the names of the slot's two holders' own base items.
            const auto name = [](RE::FormID ref, const std::string& text) {
                auto* holder = ref ? RE::TESForm::LookupByID<RE::TESObjectREFR>(ref) : nullptr;
                auto* base = holder ? holder->GetBaseObject() : nullptr;
                auto* full = base ? base->As<RE::TESFullName>() : nullptr;
                if (full) full->fullName = text.c_str();
            };
            name(m_holders[a.slot][0], Fill(a.story, a));
            // The journal shows a quest's latest log entry only: the ending carries the whole tale with it.
            if (a.phase == A::Phase::Ended) name(m_holders[a.slot][1], Fill(a.story + "\n\n" + A::TaleClosing(a), a));
        }
    }

    void ArcBridge::SetHolders(std::uint8_t slot, RE::TESObjectREFR* tale, RE::TESObjectREFR* close) {
        if (slot >= A::kSlots) return;
        {
            std::scoped_lock lock(m_mutex);
            m_holders[slot] = {tale ? tale->GetFormID() : 0u, close ? close->GetFormID() : 0u};
        }
        ApplyTitles();
    }

    void ArcBridge::Ended(const A::Arc& a) {
        SetSpirit(a.id, 0, false);
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
                const auto label = Safe::Text(entry, "display_name");
                const int id = Safe::Int(entry, "id");
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
        if (version < 1 || version > A::kRecordVersion || length > (1u << 23)) {   // older records load too; 24 stories at their longest stay under this
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
            // Its places out in the world and whoever was placed for it (made at run time: those stay as they are).
            bool lostPlace = false;
            for (std::size_t s = 0; s < A::kSites; ++s) {
                const bool had = a.site[s] != 0;
                resolve(a.site[s]);
                resolve(a.siteAnchor[s]);
                resolve(a.siteBoss[s]);
                lostPlace = lostPlace || (had && a.site[s] == 0);
            }
            for (auto& man : a.thug) resolve(man);
            // A story still running whose place left the load order cannot be finished: it is let go.
            return lostPlace && A::IsLive(a);
        });
        std::scoped_lock lock(m_mutex);
        m_book = std::move(book);
        m_pending.clear();
        m_reconciled = false;
        m_loadedAt = std::chrono::steady_clock::now();
        logger::info("Arcs: loaded {} ({} open)", m_book.all.size(), m_book.Count(A::Phase::Open));
        for (const auto& a : m_book.all) {
            if (A::IsLive(a) && a.pattern == A::Pattern::Tale) LogOutline(a);
        }
    }

    void ArcBridge::Revert() {
        HeartBridge::Forget();
        std::scoped_lock lock(m_mutex);
        g_homeDoors.clear();
        for (auto& held : g_bound) held.store(0);
        for (auto& lies : g_clues) lies.store(0);
        for (auto& one : g_spirits) {
            one.rest.store(false);
            one.ref.store(0);
            one.arc.store(0);
        }
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
        void ArcScene(RE::StaticFunctionTag*, std::int32_t id, float seconds) { ArcBridge::GetSingleton()->Scene(U(id), seconds); }
        void ArcPlanted(RE::StaticFunctionTag*, std::int32_t id, std::int32_t prop, RE::TESObjectREFR* at) {
            ArcBridge::GetSingleton()->Planted(U(id), prop, at);
        }
        std::int32_t ArcField(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) { return ArcBridge::GetSingleton()->Field(U(id), S(name)); }
        RE::TESForm* ArcForm(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) { return ArcBridge::GetSingleton()->Form(U(id), S(name)); }
        RE::BSFixedString ArcText(RE::StaticFunctionTag*, std::int32_t id, RE::BSFixedString name) {
            return RE::BSFixedString(ArcBridge::GetSingleton()->Text(U(id), S(name)).c_str());
        }
        void ArcApplyTitles(RE::StaticFunctionTag*) { ArcBridge::GetSingleton()->ApplyTitles(); }
        void ArcDebugCast(RE::StaticFunctionTag*, RE::Actor* actor) { g_forcedCast.store(actor ? actor->GetFormID() : 0); }
        void ArcSetHolders(RE::StaticFunctionTag*, std::int32_t slot, RE::TESObjectREFR* tale, RE::TESObjectREFR* close) {
            if (slot >= 0) ArcBridge::GetSingleton()->SetHolders(static_cast<std::uint8_t>(slot), tale, close);
        }
        bool ArcRequestTale(RE::StaticFunctionTag*, RE::Actor* asker) { return ArcBridge::GetSingleton()->RequestTale(asker); }
        void ArcLingerOver(RE::StaticFunctionTag*, std::int32_t id) { ArcBridge::GetSingleton()->LingerOver(U(id)); }
        void ArcBossPlaced(RE::StaticFunctionTag*, std::int32_t id, std::int32_t site, RE::Actor* boss) {
            ArcBridge::GetSingleton()->BossPlaced(U(id), site, boss);
        }
        void ArcGangPlaced(RE::StaticFunctionTag*, std::int32_t id) { ArcBridge::GetSingleton()->GangPlaced(U(id)); }
        void ArcCaptive(RE::StaticFunctionTag*, std::int32_t id, std::int32_t state) { ArcBridge::GetSingleton()->Captive(U(id), state); }
        void ArcCluePlaced(RE::StaticFunctionTag*, std::int32_t id, std::int32_t clue, RE::TESObjectREFR* lies) {
            ArcBridge::GetSingleton()->CluePlaced(U(id), clue, lies);
        }
        void ArcSpiritOut(RE::StaticFunctionTag*, std::int32_t id, RE::Actor* shade) { ArcBridge::GetSingleton()->SpiritOut(U(id), shade); }
        void ArcSpiritGone(RE::StaticFunctionTag*, std::int32_t id, std::int32_t how) { ArcBridge::GetSingleton()->SpiritGone(U(id), how); }
        bool ArcSpiritRest(RE::StaticFunctionTag*, std::int32_t id) { return ArcBridge::GetSingleton()->SpiritRest(U(id)); }
        // A haunting asked for (talk of something that walks by night; the test hook): a story of the town about a
        // shade outside one of the places near it, or nothing.
        bool ArcRequestHaunt(RE::StaticFunctionTag*, RE::Actor* giver, bool rumored) {
            if (!giver) return false;
            ArcBridge::TaleSeed seed;
            seed.kind = "haunting";
            seed.rumored = rumored;
            return ArcBridge::GetSingleton()->RequestTale(giver, {}, &seed);
        }
        // A hunt asked for (a rumor of deaths; the test hook): a story of the town about what is killing people near
        // it, with what was left of a victim to look at and what did it to face, or nothing.
        bool ArcRequestHunt(RE::StaticFunctionTag*, RE::Actor* giver, bool rumored) {
            if (!giver) return false;
            ArcBridge::TaleSeed seed;
            seed.kind = "mystery";
            seed.rumored = rumored;
            return ArcBridge::GetSingleton()->RequestTale(giver, {}, &seed);
        }
        // A rescue asked for by the old dungeon master or the dashboard: a story of the town about someone held at a
        // place near it, written and run like any other tale, or nothing.
        bool ArcRequestRescue(RE::StaticFunctionTag*, RE::Actor* giver) {
            if (!giver) return false;
            ArcBridge::TaleSeed seed;
            seed.kind = "rescue";
            const int reach = NPCIndex::ReachOf(giver);
            seed.fromAfar = reach != 0;
            seed.otherHold = reach >= 2;
            return ArcBridge::GetSingleton()->RequestTale(giver, {}, &seed);
        }
        void ArcThugPlaced(RE::StaticFunctionTag*, std::int32_t id, std::int32_t k, RE::Actor* thug) {
            ArcBridge::GetSingleton()->ThugPlaced(U(id), k, thug);
        }
        void ArcAmbushDone(RE::StaticFunctionTag*, std::int32_t id, std::int32_t what) { ArcBridge::GetSingleton()->AmbushDone(U(id), what); }
        bool ArcInWilds(RE::StaticFunctionTag*, RE::TESObjectREFR* ref) { return InTheWilds(ref); }
        void ArcOfferLater(RE::StaticFunctionTag*, std::int32_t id) { ArcBridge::GetSingleton()->OfferLater(U(id)); }
        void ArcOfferDeferred(RE::StaticFunctionTag*, std::int32_t id) { ArcBridge::GetSingleton()->OfferDeferred(U(id)); }
        RE::Actor* ArcCompany(RE::StaticFunctionTag*, std::int32_t id) { return ArcBridge::GetSingleton()->Company(U(id)); }
        // What only scripts can read of someone's heart (other mods keep it in script storage): Hearts.h, HeartBridge.
        void ArcHeart(RE::StaticFunctionTag*, RE::Actor* who, std::int32_t sparked, std::int32_t stance, std::int32_t withChild,
                      bool childIsPlayers, std::int32_t children, bool playersChild, float refusedAt) {
            if (who) HeartBridge::Told(who->GetFormID(), sparked, stance, withChild, childIsPlayers, children, playersChild, refusedAt);
        }
        std::int32_t ArcRankAfter(RE::StaticFunctionTag*, std::int32_t rank, bool closer) { return Hearts::RankAfter(rank, closer); }
        void ArcEarshot(RE::StaticFunctionTag*, float units) {
            // Whisper mode shrinks SkyrimNet's distance to a couple of steps: someone that near still stands there.
            const float earshot = std::isfinite(units) && units > 0.0f ? std::clamp(units, 500.0f, 3000.0f) : kEarshotDefault;
            if (std::abs(g_earshot.exchange(earshot) - earshot) > 1.0f) {
                logger::info("Arcs: whoever of a story stands within {:.0f} units of the player hears what is said there (SkyrimNet's interaction distance)", earshot);
            }
        }
        void ArcOfferHeldBack(RE::StaticFunctionTag*, std::int32_t id) {
            auto* bridge = ArcBridge::GetSingleton();
            auto* by = bridge->Company(U(id));
            const char* name = by ? by->GetDisplayFullName() : nullptr;
            bridge->OfferDeferred(U(id), name && *name ? name : "someone");
        }
        bool ArcHurries(RE::StaticFunctionTag*, std::int32_t id, RE::Actor* actor) {
            return ArcBridge::GetSingleton()->Hurries(id > 0 ? U(id) : 0u, actor);
        }
        std::int32_t ArcReachOf(RE::StaticFunctionTag*, RE::Actor* actor) { return actor ? NPCIndex::ReachOf(actor) : 2; }
        // Test hook: how the director's distance rule sees someone: reach (0 here, 1 the hold, 2 another hold), plus 10
        // when they are close to the player.
        std::int32_t ArcDebugReach(RE::StaticFunctionTag*, RE::Actor* actor) {
            if (!actor) return -1;
            const int reach = NPCIndex::ReachOf(actor);
            const bool bonded = LivingWorldBridge::GetSingleton()->CloseToPlayer(actor);
            logger::info("[Director] test: {} is {}{}; someone came from afar within {:.0f} days: {}", actor->GetDisplayFullName(),
                         reach == 0 ? "here" : reach == 1 ? "elsewhere in the hold" : "in another hold",
                         bonded ? ", close to the player" : ", not close to the player", StoryDirector::kFarRestDays,
                         ArcBridge::GetSingleton()->FarComerLately(StoryDirector::kFarRestDays) ? "yes" : "no");
            return reach + (bonded ? 10 : 0);
        }
        // Test hook: a tale as the director asks for one (a reason of the asker's own, coming from afar).
        bool ArcRequestTaleWhy(RE::StaticFunctionTag*, RE::Actor* asker, RE::BSFixedString why) {
            // As the director would seed it: from another hold when that is where they are.
            ArcBridge::TaleSeed seed{why.c_str() ? why.c_str() : "", "seek_player", true, NPCIndex::ReachOf(asker) >= 2};
            return ArcBridge::GetSingleton()->RequestTale(asker, {}, &seed);
        }
        bool ArcFetchProp(RE::StaticFunctionTag*, std::int32_t id, std::int32_t prop) { return ArcBridge::GetSingleton()->FetchProp(U(id), prop); }
        // Test hook: a tale written by hand (a JSON file at an absolute path), cast from the asker's town like any other.
        bool ArcDebugTaleFile(RE::StaticFunctionTag*, RE::Actor* asker, RE::BSFixedString path) {
            std::ifstream file(std::filesystem::path(path.c_str() ? path.c_str() : ""), std::ios::binary);
            if (!file) {
                logger::warn("Tales: no test tale at '{}'", path.c_str() ? path.c_str() : "");
                return false;
            }
            const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
            return ArcBridge::GetSingleton()->RequestTale(asker, text);
        }
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
        a_vm->RegisterFunction("ArcScene", script, ArcScene); ++count;
        a_vm->RegisterFunction("ArcPlanted", script, ArcPlanted); ++count;
        a_vm->RegisterFunction("ArcField", script, ArcField); ++count;
        a_vm->RegisterFunction("ArcForm", script, ArcForm); ++count;
        a_vm->RegisterFunction("ArcText", script, ArcText); ++count;
        a_vm->RegisterFunction("ArcApplyTitles", script, ArcApplyTitles); ++count;
        a_vm->RegisterFunction("ArcDebugCast", script, ArcDebugCast); ++count;
        a_vm->RegisterFunction("ArcSetHolders", script, ArcSetHolders); ++count;
        a_vm->RegisterFunction("ArcRequestTale", script, ArcRequestTale); ++count;
        a_vm->RegisterFunction("ArcLingerOver", script, ArcLingerOver); ++count;
        a_vm->RegisterFunction("ArcBossPlaced", script, ArcBossPlaced); ++count;
        a_vm->RegisterFunction("ArcGangPlaced", script, ArcGangPlaced); ++count;
        a_vm->RegisterFunction("ArcCaptive", script, ArcCaptive); ++count;
        a_vm->RegisterFunction("ArcCluePlaced", script, ArcCluePlaced); ++count;
        a_vm->RegisterFunction("ArcRequestHunt", script, ArcRequestHunt); ++count;
        a_vm->RegisterFunction("ArcRequestHaunt", script, ArcRequestHaunt); ++count;
        a_vm->RegisterFunction("ArcSpiritOut", script, ArcSpiritOut); ++count;
        a_vm->RegisterFunction("ArcSpiritGone", script, ArcSpiritGone); ++count;
        a_vm->RegisterFunction("ArcSpiritRest", script, ArcSpiritRest); ++count;
        a_vm->RegisterFunction("ArcRequestRescue", script, ArcRequestRescue); ++count;
        a_vm->RegisterFunction("ArcThugPlaced", script, ArcThugPlaced); ++count;
        a_vm->RegisterFunction("ArcAmbushDone", script, ArcAmbushDone); ++count;
        a_vm->RegisterFunction("ArcInWilds", script, ArcInWilds); ++count;
        a_vm->RegisterFunction("ArcDebugReach", script, ArcDebugReach); ++count;
        a_vm->RegisterFunction("ArcOfferLater", script, ArcOfferLater); ++count;
        a_vm->RegisterFunction("ArcOfferDeferred", script, ArcOfferDeferred); ++count;
        a_vm->RegisterFunction("ArcCompany", script, ArcCompany); ++count;
        a_vm->RegisterFunction("ArcEarshot", script, ArcEarshot); ++count;
        a_vm->RegisterFunction("ArcHeart", script, ArcHeart); ++count;
        a_vm->RegisterFunction("ArcRankAfter", script, ArcRankAfter); ++count;
        a_vm->RegisterFunction("ArcOfferHeldBack", script, ArcOfferHeldBack); ++count;
        a_vm->RegisterFunction("ArcHurries", script, ArcHurries); ++count;
        a_vm->RegisterFunction("ArcReachOf", script, ArcReachOf); ++count;
        a_vm->RegisterFunction("ArcRequestTaleWhy", script, ArcRequestTaleWhy); ++count;
        a_vm->RegisterFunction("ArcFetchProp", script, ArcFetchProp); ++count;
        a_vm->RegisterFunction("ArcDebugTaleFile", script, ArcDebugTaleFile); ++count;
    }
}
