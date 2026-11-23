#include "LivingWorldBridge.h"
#include "GameText.h"
#include "BondBridge.h"
#include "RumorBridge.h"
#include "Safe.h"
#include "QuestBridge.h"
#include "ArcBridge.h"

#include "DialogueTracker.h"
#include "ProcessUtils.h"
#include "SkyrimNetAPI.h"
#include "SpeechTracker.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <string_view>
#include <thread>
#include <unordered_set>

// 4.0: the lives of followers the player left behind. A follower who stopped following (and is not away on one of
// the player's errands) is left behind; after a while apart (a week by default, a month when told to wait) their own
// life begins: work at an inn of the hold, an adventure at a dungeon now and then (sometimes with another former
// follower), a trip to a town, home for a while. Every step is made true at once and only while nobody sees: they are
// moved there for real, they remember it (a SkyrimNet memory) and people of the holds concerned may have heard of it
// (a world-knowledge entry, found only when a conversation is about them, so it costs nothing otherwise). Rejoining
// the player, an errand or a story takes the package of their own life off again. The scripts do the moving and the
// packages (IntelEngine_Lives.psc); the record and every choice are here and in LivingWorld.h.
//
// And a life shows (2026-11-23): each step goes round the holds as talk of the day (the rumor ledger: a publican
// elsewhere may bring it up); coming back from an adventure is decided then and made true (a find in their pack, a
// wound in their memory, now and then a keepsake set aside for the player); the first meeting after days apart is a
// reunion, where their greeting and their prompt carry what passed; and a keepsake changes hands face to face.

namespace IntelEngine {

    namespace {
        using LivingWorld::Activity;
        using LivingWorld::Life;

        // Work that runs inside the game's frame (Track is called from the scripts) says how long it took once that
        // is long enough to notice, so a hitch on a big load order shows in the log instead of being guessed at.
        struct FrameCost {
            const char* what;
            double noticeMs;
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            // Where the time went, for the slowest run: the end of each part in ms from the start (0 = not reached),
            // and how many actors the look went through.
            std::array<double, 3> parts{};
            int actors = 0;
            double Now() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
            void Part(std::size_t i) { parts[i] = Now(); }
            ~FrameCost() {
                const double ms = Now();
                if (ms >= noticeMs) logger::info("LivingWorld: {} took {:.1f} ms of the game's frame", what, ms);
                // And how long it takes as a rule: the slowest of every 30, now and then, with where its time went.
                static int runs = 0;
                static double slowest = -1.0;
                static std::array<double, 3> slowestParts{};
                static int slowestActors = 0;
                if (ms > slowest) {
                    slowest = ms;
                    slowestParts = parts;
                    slowestActors = actors;
                }
                if (++runs >= 30) {
                    logger::info("LivingWorld: {}: at most {:.2f} ms of the game's frame in the last {} runs (that one: its slice of "
                                 "{} actors done by {:.2f} ms, SkyrimNet asked by {:.2f} ms, the lock had by {:.2f} ms)",
                                 what, slowest, runs, slowestActors, slowestParts[0], slowestParts[1], slowestParts[2]);
                    runs = 0;
                    slowest = -1.0;
                }
            }
        };

        constexpr std::string_view kRumorLabel = "IntelEngine life: ";
        constexpr float kGreetDistance = 1200.0f;

        RE::BGSLocation* HoldOf(RE::BGSLocation* location) {
            for (auto* loc = location; loc; loc = loc->parentLoc) {
                if (loc->HasKeywordString("LocTypeHold")) return loc;
            }
            return nullptr;
        }

        RE::FormID HoldIdOf(RE::FormID location) {
            auto* hold = HoldOf(location ? RE::TESForm::LookupByID<RE::BGSLocation>(location) : nullptr);
            return hold ? hold->GetFormID() : 0;
        }

        std::string LocationName(RE::FormID location) {
            auto* loc = location ? RE::TESForm::LookupByID<RE::BGSLocation>(location) : nullptr;
            const char* name = loc ? GameText::In(loc->GetFullName()) : nullptr;
            return name ? name : "";
        }

        // A place's name as people say it in the middle of a sentence: an inn with its article and its town ("the
        // Sleeping Giant Inn in Riverwood", "the Bannered Mare in Whiterun"), a dungeon or town as it is named.
        std::string PlaceName(RE::FormID location) {
            auto* loc = location ? RE::TESForm::LookupByID<RE::BGSLocation>(location) : nullptr;
            const char* name = loc ? GameText::In(loc->GetFullName()) : nullptr;
            if (!name || !*name) return "";
            std::string text = name;
            if (text.starts_with("The ")) {
                text[0] = 't';
            } else if (loc->HasKeywordString("LocTypeInn") && !text.starts_with("the ")) {
                text = "the " + text;
            }
            if (loc->HasKeywordString("LocTypeInn") && loc->parentLoc) {
                const char* town = GameText::In(loc->parentLoc->GetFullName());
                if (town && *town && !loc->parentLoc->HasKeywordString("LocTypeHold")) text += std::string(" in ") + town;
            }
            return text;
        }

        // Skyrim and Solstheim, where the player can come upon someone (not Blackreach, Sovngarde or a mod's realm).
        bool InMainWorld(RE::TESObjectREFR* ref) {
            int depth = 0;
            for (auto* world = ref ? ref->GetWorldspace() : nullptr; world && depth < 4; world = world->parentWorld, ++depth) {
                const char* id = world->GetFormEditorID();
                if (id && (_stricmp(id, "Tamriel") == 0 || _stricmp(id, "DLC2SolstheimWorld") == 0)) return true;
            }
            return false;
        }

        struct InsideDoor {
            RE::TESObjectREFR* door = nullptr;
            RE::NiPoint3 standAt{};
            bool known = false;
        };

        // An inn's front door on the inside, and where someone coming in through it stands (on the inn's floor).
        InsideDoor FrontDoorInside(RE::TESObjectCELL* cell) {
            InsideDoor best;
            cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                auto* base = ref ? ref->GetBaseObject() : nullptr;
                if (!base || !base->Is(RE::FormType::Door) || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
                auto* teleport = ref->extraList.GetByType<RE::ExtraTeleport>();
                if (!teleport || !teleport->teleportData) return RE::BSContainer::ForEachResult::kContinue;
                auto outside = std::as_const(teleport->teleportData->linkedDoor).get();
                if (!outside) return RE::BSContainer::ForEachResult::kContinue;
                auto* outsideCell = outside->GetSaveParentCell();
                if (!outsideCell) outsideCell = outside->GetParentCell();
                if (!outsideCell || outsideCell->IsInteriorCell() || !InMainWorld(outside.get())) {
                    return RE::BSContainer::ForEachResult::kContinue;
                }
                if (best.door && ref->GetPositionZ() >= best.door->GetPositionZ()) return RE::BSContainer::ForEachResult::kContinue;
                best.door = ref;
                auto* back = outside->extraList.GetByType<RE::ExtraTeleport>();
                best.known = back && back->teleportData;
                if (best.known) best.standAt = back->teleportData->position;
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return best;
        }

        // Someone a life can be about: a grown person, alive, not a summon.
        bool Person(RE::Actor* actor) {
            // ActorTypeNPC by its form id and the engine's own keyword test (race included), as the rest of IntelEngine
            // does: looked up by name, the race's keyword was not found and no follower ever counted.
            static auto* npcKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);
            if (!actor || actor->IsPlayerRef() || actor->IsDead() || actor->IsCommandedActor()) return false;
            auto* race = actor->GetRace();
            if (race && race->data.flags.any(RE::RACE_DATA::Flag::kChild)) return false;
            return npcKeyword && actor->HasKeyword(npcKeyword);
        }

        struct Ties {
            bool errand = false;   // an IntelEngine task agent or target
            bool journal = false;  // the target of an objective the player has open (IntelEngine's)
            bool quest = false;    // in a running quest of the journal's kinds
            bool home = false;     // married to the player, or engaged: keeps the home
        };

        Ties TiesOf(RE::Actor* actor, bool skipStorylines = false) {
            Ties t;
            auto* aliases = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (!aliases) return t;
            RE::BSReadLockGuard guard(aliases->lock);
            for (auto* instance : aliases->aliases) {
                if (!instance || !instance->quest || !instance->alias) continue;
                auto* quest = instance->quest;
                const char* rawId = quest->GetFormEditorID();
                const std::string_view id = rawId ? rawId : "";
                const std::string_view alias = instance->alias->aliasName.c_str() ? instance->alias->aliasName.c_str() : "";
                if (id == "IntelEngine") {
                    if (alias.starts_with("AgentAlias") || alias.starts_with("TargetAlias")) t.errand = true;
                    else if (alias == "QuestTarget") t.journal = true;
                    continue;
                }
                if (id == "IntelEngine_Threads") {
                    if (!skipStorylines) t.journal = true;
                    continue;
                }
                // 4.0 quests: the one who asked is the journal's target; a former follower on a venture with the
                // player is on the player's errand.
                if (id.starts_with("IntelEngine_Quest") && id != "IntelEngine_QuestKeeper") {
                    if (alias == "Ally" || alias == "Companion") t.errand = true;
                    else if (alias == "Giver") t.journal = true;
                    continue;
                }
                if (id.starts_with("IntelEngine")) continue;
                if (id.starts_with("RelationshipMarriage")) {
                    t.home = true;
                    continue;
                }
                if (!quest->IsRunning()) continue;
                // The target of an objective shown in the journal, of any quest (a favour too): never moved.
                bool inJournal = false;
                for (auto* objective : quest->objectives) {
                    if (!objective) continue;
                    const auto state = objective->state.get();
                    if (state == RE::QUEST_OBJECTIVE_STATE::kDisplayed || state == RE::QUEST_OBJECTIVE_STATE::kCompletedDisplayed ||
                        state == RE::QUEST_OBJECTIVE_STATE::kFailedDisplayed) {
                        inJournal = true;
                    }
                    if (state != RE::QUEST_OBJECTIVE_STATE::kDisplayed || !objective->targets) continue;
                    for (std::uint32_t i = 0; i < objective->numTargets; ++i) {
                        auto* target = objective->targets[i];
                        if (target && target->alias == instance->alias->aliasID) t.journal = true;
                    }
                }
                // Only a quest the player is on (it shows in the journal) holds its people. Many quests run from the
                // start of a game with their people in aliases long before the player hears of them ("In My Time of
                // Need" holds Saadia); counting those kept them from every visit, which 3.8 never did.
                const auto type = quest->GetType();
                if (inJournal && type != RE::QUEST_DATA::Type::kNone && type != RE::QUEST_DATA::Type::kMiscellaneous) t.quest = true;
            }
            return t;
        }

        bool NearPlayer(RE::Actor* actor, RE::PlayerCharacter* player, float within) {
            if (!actor->Is3DLoaded()) return false;
            auto* a = actor->GetParentCell();
            auto* p = player->GetParentCell();
            if (!a || !p) return false;
            if ((a->IsInteriorCell() || p->IsInteriorCell()) && a != p) return false;
            return actor->GetPosition().GetDistance(player->GetPosition()) < within;
        }

        // Could the player see this spot, or someone at it? Then nothing may appear or vanish there now.
        bool PlayerCouldSee(RE::TESObjectREFR* ref, RE::PlayerCharacter* player) {
            if (!ref) return true;
            auto* pc = player->GetParentCell();
            auto* rc = ref->GetSaveParentCell();
            if (!rc) rc = ref->GetParentCell();
            if (rc && pc && rc == pc) return true;
            // A player indoors sees only that room, one outdoors no room: whatever is loaded. Outdoor cells the player
            // walked through stay loaded a while, so "3D loaded" alone kept Jenassa in Riverwood "where the player
            // could see" while the player sat in the Bannered Mare, and her own life never began (2026-11-17).
            if (pc && rc && (pc->IsInteriorCell() || rc->IsInteriorCell())) return false;
            if (ref->Is3DLoaded()) return true;
            if (!pc || pc->IsInteriorCell() || (rc && rc->IsInteriorCell())) return false;
            if (ref->GetWorldspace() != player->GetWorldspace()) return false;
            return ref->GetPosition().GetDistance(player->GetPosition()) < 10000.0f;
        }

        RE::FormID LocationOf(RE::Actor* actor) {
            auto* location = actor->GetCurrentLocation();
            return location ? location->GetFormID() : 0;
        }

        std::string Escape(const std::string& text) {
            std::string out;
            for (const char c : text) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        }

        void Replace(std::string& text, std::string_view key, const std::string& value) {
            for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
                text.replace(at, key.size(), value);
            }
        }

        bool SkyrimNetWritable() {
            return SkyrimNetAPI::IsMemorySystemReady && SkyrimNetAPI::IsMemorySystemReady() && SkyrimNetAPI::AddMemory;
        }

        // ---- what passed while apart, told (LivingWorld.h "what passed while apart, told")

        // Greetings come one at a time: a handful of old companions in one town do not hail the player in a chorus.
        constexpr auto kGreetGap = std::chrono::seconds(45);
        // Somebody else said a line to the player this recently: the player is in talk with them, and whoever has
        // something of their own waits their turn.
        constexpr float kOthersTalkSeconds = 30.0f;

        std::string PersonName(RE::FormID id) {
            auto* actor = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
            return actor ? std::string(Safe::Name(GameText::In(actor->GetDisplayFullName()))) : std::string();
        }

        std::string ItemName(RE::FormID id) {
            auto* form = id ? RE::TESForm::LookupByID(id) : nullptr;
            const char* name = form ? GameText::In(form->GetName()) : nullptr;
            return name ? name : "";
        }

        std::string PlayerName() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            return player ? std::string(Safe::Name(GameText::In(player->GetDisplayFullName()))) : std::string();
        }

        LivingWorld::Names NamesOf(RE::FormID self, const std::string& player) {
            LivingWorld::Names n;
            n.place = [](std::uint32_t id) { return PlaceName(id); };
            n.person = [](std::uint32_t id) { return PersonName(id); };
            n.item = [](std::uint32_t id) { return ItemName(id); };
            n.player = player;
            n.self = PersonName(self);
            return n;
        }

        bool Asleep(RE::Actor* actor) {
            auto* state = actor ? actor->AsActorState() : nullptr;
            if (!state) return false;
            using S = RE::SIT_SLEEP_STATE;
            const auto now = state->GetSitSleepState();
            return now == S::kWantToSleep || now == S::kWaitingForSleepAnim || now == S::kIsSleeping || now == S::kWantToWake;
        }

        // Saying something now, or a moment ago: whatever is said to them is part of that talk.
        bool TalkingNow(RE::FormID who) {
            auto* speech = SpeechTracker::GetSingleton();
            const float spoke = speech->SecondsSinceSpeechEnded(who);
            return speech->IsSpeechPending(who) || (spoke >= 0.0f && spoke <= 30.0f);
        }

        bool OthersHaveTheEar(RE::FormID who) {
            const float other = DialogueTracker::GetSingleton()->SecondsSinceOtherSpokeToPlayer(who);
            return other >= 0.0f && other < kOthersTalkSeconds;
        }

        // Where they belong by the game's own record (an inn for a hireling, a house, a town): "home".
        RE::FormID HomeOf(RE::Actor* actor) {
            auto* home = actor ? actor->GetEditorLocation() : nullptr;
            return home ? home->GetFormID() : 0;
        }

        // What an adventure of their own may bring back: gems of the game's own records (Skyrim.esm 1.5.97, the same in
        // every language and load order; the ones the tales use), those this game has.
        RE::FormID SpoilsItem(std::uint8_t pick) {
            static const std::vector<RE::FormID> gems = [] {
                std::vector<RE::FormID> out;
                for (const RE::FormID id : {0x00063B42u, 0x00063B45u, 0x00063B46u, 0x00063B44u, 0x00063B43u}) {
                    if (RE::TESForm::LookupByID<RE::TESBoundObject>(id)) out.push_back(id);
                }
                return out;
            }();
            return gems.empty() ? 0 : gems[pick % gems.size()];
        }

        // Into their pack, on the game's own thread: what an adventure brought back is real (whoever trades with them or
        // looks in their pack finds it), and only a keepsake ever leaves it for the player, by hand.
        void Pack(RE::FormID who, RE::FormID item, std::uint16_t gold) {
            if (!item && !gold) return;
            auto* task = SKSE::GetTaskInterface();
            if (!task) return;
            task->AddTask([who, item, gold]() {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(who);
                if (!actor || actor->IsDead()) return;
                if (auto* thing = item ? RE::TESForm::LookupByID<RE::TESBoundObject>(item) : nullptr) actor->AddObjectToContainer(thing, nullptr, 1, nullptr);
                if (auto* coin = gold ? RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F) : nullptr) {
                    actor->AddObjectToContainer(coin, nullptr, static_cast<std::int32_t>(gold), nullptr);
                }
            });
        }
    }

    // ---------------------------------------------------------------- the places a life can go

    void LivingWorldBridge::BuildCatalogue() {
        m_catalogued = true;
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) return;
        const auto started = std::chrono::steady_clock::now();
        std::vector<Spot> inns, dungeons, towns;
        std::unordered_set<RE::FormID> seen;
        std::size_t cells = 0;
        std::size_t locations = 0;
        for (auto* cell : data->interiorCells) {
            if (!cell) continue;
            ++cells;
            auto* location = cell->GetLocation();
            if (!location || seen.contains(location->GetFormID())) continue;
            if (!location->HasKeywordString("LocTypeInn") || location->HasKeywordString("LocTypePlayerHouse")) continue;
            const char* name = GameText::In(location->GetFullName());
            if (!name || !*name) continue;
            const auto inside = FrontDoorInside(cell);
            if (!inside.door) continue;
            seen.insert(location->GetFormID());
            Spot spot;
            spot.location = location->GetFormID();
            auto* hold = HoldOf(location);
            spot.hold = hold ? hold->GetFormID() : 0;
            spot.ref = inside.door->GetFormID();
            if (inside.known) spot.offset = inside.standAt - inside.door->GetPosition();
            inns.push_back(spot);
        }
        for (auto* location : data->GetFormArray<RE::BGSLocation>()) {
            if (!location) continue;
            ++locations;
            const char* name = GameText::In(location->GetFullName());
            if (!name || !*name || location->HasKeywordString("LocTypePlayerHouse")) continue;
            auto marker = std::as_const(location->worldLocMarker).get();
            if (!marker || !InMainWorld(marker.get())) continue;
            const bool dungeon = IsDungeon(location);
            const bool town = !dungeon && (location->HasKeywordString("LocTypeCity") || location->HasKeywordString("LocTypeTown"));
            if (!dungeon && !town) continue;
            Spot spot;
            spot.location = location->GetFormID();
            auto* hold = HoldOf(location);
            spot.hold = hold ? hold->GetFormID() : 0;
            spot.ref = marker->GetFormID();
            (dungeon ? dungeons : towns).push_back(spot);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        logger::info("LivingWorld: places for lives: {} inns, {} dungeons, {} towns ({} interior cells and {} locations "
                     "looked at once, {:.1f} ms of the game's frame)",
                     inns.size(), dungeons.size(), towns.size(), cells, locations, ms);
        std::scoped_lock lock(m_mutex);
        m_inns = std::move(inns);
        m_dungeons = std::move(dungeons);
        m_towns = std::move(towns);
    }

    void LivingWorldBridge::CatalogPlaces() {
        if (!m_catalogued) BuildCatalogue();
    }

    std::vector<LivingWorldBridge::TownMark> LivingWorldBridge::Towns() {
        if (!m_catalogued) BuildCatalogue();        // the main menu has done it; this is for a caller that comes before
        std::scoped_lock lock(m_mutex);
        std::vector<TownMark> out;
        out.reserve(m_towns.size());
        for (const auto& spot : m_towns) out.push_back({spot.location, spot.hold, spot.ref});
        return out;
    }

    const LivingWorldBridge::Spot* LivingWorldBridge::FindSpot(RE::FormID location) const {
        for (const auto* list : {&m_inns, &m_dungeons, &m_towns}) {
            for (const auto& spot : *list) {
                if (spot.location == location) return &spot;
            }
        }
        return nullptr;
    }

    // ---------------------------------------------------------------- following what is true

    void LivingWorldBridge::Track() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) return;
        if (!m_catalogued) BuildCatalogue();
        FrameCost cost{"a look at who follows and who lives their own life", 2.0};

        // Who serves the player: every actor of all four process tiers is asked (someone just moved in from an
        // unloaded cell is not "high" yet). That is every actor the game keeps in memory, thousands on a big load
        // order: all 2129 of the test profile at once took 0.21 ms of one frame (2026-11-19, the first time this part
        // was timed by itself), and more with more mods. So a slice a look, at most kScanMs of the frame, and the
        // answer is the last pass that was finished. A new follower is known a look or two later; one who has left
        // is seen at once all the same, because each life is asked directly below.
        constexpr double kScanMs = 0.12;
        if (const auto epoch = m_scanEpoch.load(); epoch != m_scanEpochSeen) {
            m_scanEpochSeen = epoch;
            m_scanHandles.clear();
            m_scanFound.clear();
            m_servingIds.clear();
            m_scanAt = 0;
            m_servingKnown = false;
            m_collecting = false;
        }
        if (!m_collecting && m_scanAt >= m_scanHandles.size()) {
            m_scanHandles.clear();
            m_scanFound.clear();
            m_scanAt = 0;
            m_collecting = true;
            m_collectTier = 0;
            m_collectAt = 0;
        }
        if (m_collecting) {
            // Gathering the handles is work of its own on a big load order (copying 9,800 of them took 0.49 ms of one
            // frame, 2026-11-21): a part a look, like the asking. A list that changes between two looks may be read
            // with one actor twice or one missed; the next pass has them.
            auto* lists = RE::ProcessLists::GetSingleton();
            const RE::BSTArray<RE::ActorHandle>* tiers[4] = {lists ? &lists->highActorHandles : nullptr,
                                                             lists ? &lists->middleHighActorHandles : nullptr,
                                                             lists ? &lists->middleLowActorHandles : nullptr,
                                                             lists ? &lists->lowActorHandles : nullptr};
            bool outOfTime = false;
            while (lists && m_collectTier < 4 && !outOfTime) {
                const auto& tier = *tiers[m_collectTier];
                while (m_collectAt < tier.size()) {
                    m_scanHandles.push_back(tier[m_collectAt]);
                    ++m_collectAt;
                    if ((m_collectAt & 255u) == 0 && cost.Now() >= kScanMs) {
                        outOfTime = true;
                        break;
                    }
                }
                if (m_collectAt >= tier.size()) {
                    ++m_collectTier;
                    m_collectAt = 0;
                }
            }
            if (!lists || m_collectTier >= 4) m_collecting = false;
        }
        if (!m_collecting) {
            while (m_scanAt < m_scanHandles.size()) {
                if (auto actor = m_scanHandles[m_scanAt].get(); actor && actor->IsPlayerTeammate()) m_scanFound.push_back(actor->GetFormID());
                ++m_scanAt;
                if ((m_scanAt & 63u) == 0 && cost.Now() >= kScanMs) break;
            }
            if (m_scanAt >= m_scanHandles.size()) {
                m_servingIds = m_scanFound;
                m_scanCount = static_cast<int>(m_scanHandles.size());
                m_servingKnown = true;
            }
        }
        std::vector<RE::Actor*> serving;
        const int loaded = m_scanCount;
        for (const auto servingId : m_servingIds) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(servingId);
            if (!actor || !actor->IsPlayerTeammate()) continue;
            if (Person(actor)) {
                serving.push_back(actor);
                continue;
            }
            // A teammate this does not count (a summon, a creature, a child): say why, once per actor and session.
            static std::unordered_set<RE::FormID> explained;
            if (explained.insert(actor->GetFormID()).second) {
                static auto* npcKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);
                auto* race = actor->GetRace();
                logger::info("LivingWorld: {} serves the player but has no life of their own here (dead {}, commanded {}, "
                             "child {}, person {})",
                             GameText::In(actor->GetDisplayFullName()), actor->IsDead(), actor->IsCommandedActor(),
                             race && race->data.flags.any(RE::RACE_DATA::Flag::kChild), npcKeyword && actor->HasKeyword(npcKeyword));
            }
        }
        cost.actors = static_cast<int>(m_scanHandles.size());
        cost.Part(0);

        // Asked before the lock: SkyrimNet's threads call this bridge's decorators while they hold locks of their own.
        const bool writable = SkyrimNetWritable();
        // Manners for whoever greets the player or hands them something: never into a scene or the minutes after one,
        // at a player with nothing on, or asleep (the private moments of Patrols.h that hold for someone already there).
        const auto privacy = PrivateMoment();
        const bool mannersHold = privacy == Patrols::Private::Scene || privacy == Patrols::Private::AfterScene ||
                                 player->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) == nullptr || Asleep(player);
        cost.Part(1);
        const float now = NowDays();
        std::string followers;
        for (auto* actor : serving) {
            if (!followers.empty()) followers += ", ";
            followers += Safe::Name(GameText::In(actor->GetDisplayFullName()));
        }
        std::scoped_lock lock(m_mutex);
        cost.Part(2);
        if (m_servingKnown && followers != m_lastFollowers) {
            logger::info("LivingWorld: following the player now: {} ({} actors loaded)", followers.empty() ? "nobody" : followers, loaded);
            m_lastFollowers = followers;
        }
        if (!m_settingsKnown) return;  // the scripts send the settings first, on every load
        const auto settings = m_settings;
        if (m_order.kind != OrderKind::None) {
            // An order the scripts never finished (an error there) must not stop every life for good.
            if (std::chrono::steady_clock::now() - m_orderAt < std::chrono::seconds(60)) return;
            logger::warn("LivingWorld: an order for {:08X} was never finished; dropping it", m_order.actor);
            m_order = Order{};
        }
        if (settings.level != LivingWorld::WorldLevel::Classic) {
            for (auto* actor : serving) Touch(actor->GetFormID()).flags |= LivingWorld::kWasFollower;
        }

        std::vector<RE::FormID> dead;
        for (auto& life : m_lives) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
            if (!actor) continue;  // not in memory now (an unloaded, non-persistent NPC): leave the record as it is
            if (actor->IsDead()) {
                dead.push_back(life.actor);
                continue;
            }
            const bool lives = settings.level == LivingWorld::WorldLevel::Lives;
            if (!lives) {
                // Below the Lives level nobody lives a life of their own: the ones under way end.
                if (LivingWorld::OwnLife(life)) life.flags |= LivingWorld::kNeedsRelease;
                if (settings.level == LivingWorld::WorldLevel::Classic) continue;
            }
            auto ties = TiesOf(actor);
            // A quest planned with them that has no journal alias yet (a venture on its way): theirs is the player's errand.
            if (QuestBridge::GetSingleton()->Involves(life.actor)) ties.errand = true;
            // Someone with a part in an open story stays where the story has them (a tale leads the player to an old
            // companion wherever their life took them: they do not move on in the middle of it).
            if (ArcBridge::GetSingleton()->Involves(life.actor)) ties.errand = true;
            const bool teammate = actor->IsPlayerTeammate();
            const bool wasServing = (life.flags & LivingWorld::kWithPlayer) != 0;
            const bool wasAway = (life.flags & LivingWorld::kOnTask) != 0 || life.activity == Activity::OnTask;
            auto set = [&life](std::uint16_t flag, bool on) {
                life.flags = static_cast<std::uint16_t>(on ? (life.flags | flag) : (life.flags & ~flag));
            };
            const bool close = NearPlayer(actor, player, 6000.0f);
            // Near again someone the player had not been near for days: a reunion begins (their greeting tells what
            // passed, and their prompt holds it a while). Asked before lastMet moves on to now.
            if (close && lives && !ties.errand && LivingWorld::BeginsReunion(life, now, teammate)) {
                life.metAgainAt = now;
                life.apartDays = now - life.lastMet;
                life.flags &= ~LivingWorld::kReunionTold;
                logger::info("LivingWorld: the player is near {} again after {:.1f} days apart ({} step(s) of their own life to tell{})",
                             GameText::In(actor->GetDisplayFullName()), life.apartDays, life.pastCount, life.keepsake ? ", a keepsake kept" : "");
            }
            // Taken along again before the greeting: that is their meeting, and what passed is theirs to tell from now.
            if (teammate && LivingWorld::ReunionOpen(life, now) && !LivingWorld::Has(life, LivingWorld::kReunionTold)) {
                LivingWorld::ReunionTold(life, now);
            }
            set(LivingWorld::kNearPlayer, close);
            if (close) life.lastMet = now;
            set(LivingWorld::kSeenRecently, now - life.lastMet < 0.25f);
            set(LivingWorld::kOnTask, ties.errand);
            set(LivingWorld::kInScene, actor->GetCurrentScene() != nullptr);
            set(LivingWorld::kQuestTarget, ties.journal);
            set(LivingWorld::kQuestBound, ties.quest);
            set(LivingWorld::kKeepsShop, ties.home || actor->GetVendorFaction() != nullptr);

            if (teammate) {
                life.flags |= LivingWorld::kWasFollower | LivingWorld::kWithPlayer;
                const bool waiting = actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kWaitingForPlayer) > 0.0f;
                if (waiting) {
                    if (!(life.flags & LivingWorld::kWaitOrder) || life.parkedSince < 0.0f) {
                        life.parkedSince = now;
                        life.parkedAt = LocationOf(actor);
                        LivingWorld::ForgetPast(life);
                        logger::info("LivingWorld: {} waits for the player at '{}'", GameText::In(actor->GetDisplayFullName()), LocationName(life.parkedAt));
                    }
                    life.flags |= LivingWorld::kWaitOrder;
                } else {
                    life.flags &= ~LivingWorld::kWaitOrder;
                    life.parkedSince = -1.0f;
                    life.parkedAt = 0;
                    life.steps = 0;
                    life.lastMet = now;
                }
                if (LivingWorld::OwnLife(life) || life.rumor != 0) {
                    life.flags |= LivingWorld::kNeedsRelease;  // back with the player in the middle of a life of their own
                } else {
                    life.activity = waiting ? Activity::Waiting : Activity::WithPlayer;
                }
            } else if (ties.errand) {
                life.flags &= ~LivingWorld::kWithPlayer;
                if (LivingWorld::OwnLife(life)) life.flags |= LivingWorld::kNeedsRelease;
                else life.activity = Activity::OnTask;
            } else {
                life.flags &= ~LivingWorld::kWithPlayer;
                if ((wasServing || wasAway) && (life.flags & LivingWorld::kWasFollower) && !LivingWorld::OwnLife(life)) {
                    // Dismissed, or back from an errand on their own: left behind from now (a wait counts from its start).
                    if (life.parkedSince < 0.0f) {
                        life.parkedSince = now;
                        life.parkedAt = LocationOf(actor);
                        life.steps = 0;
                        life.flags &= ~LivingWorld::kGreeted;
                        LivingWorld::ForgetPast(life);   // a new parting: a new story to tell when they meet again
                        logger::info("LivingWorld: the player left {} at '{}'", GameText::In(actor->GetDisplayFullName()), LocationName(life.parkedAt));
                    }
                    life.activity = Activity::Home;
                    life.nextChange = std::max(life.nextChange, now + 1.0f);
                } else if (wasAway && life.activity == Activity::OnTask) {
                    life.activity = Activity::Home;
                }
            }
        }
        for (const auto id : dead) {
            auto it = std::find_if(m_lives.begin(), m_lives.end(), [id](const Life& l) { return l.actor == id; });
            if (it == m_lives.end()) continue;
            if (LivingWorld::Has(*it, LivingWorld::kWasFollower)) logger::info("LivingWorld: {:08X} is dead; their life ends", id);
            DropRumor(*it);
            for (auto& other : m_lives) {
                if (other.companion == id) other.companion = 0;
            }
            m_lives.erase(it);
        }

        if (!m_reconciled && writable && std::chrono::steady_clock::now() - m_loadedAt > std::chrono::seconds(20)) {
            m_reconciled = true;
            Reconcile();
        }
        PlanOne(now, settings, writable, mannersHold);
    }

    // ---------------------------------------------------------------- choosing the next change

    bool LivingWorldBridge::PlanOne(float now, const LivingWorld::Settings& settings, bool writable, bool mannersHold) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        // Endings first: someone back with the player must follow at once, not sandbox where their life was.
        for (const auto& life : m_lives) {
            if (!(life.flags & LivingWorld::kNeedsRelease)) continue;
            m_order = Order{};
            m_order.kind = OrderKind::Release;
            m_order.actor = life.actor;
            m_orderAt = std::chrono::steady_clock::now();
            return true;
        }
        if (settings.level != LivingWorld::WorldLevel::Lives) return false;

        // The player comes upon someone living their own life: they notice, once per step of it. Meeting again after
        // days apart is more, a reunion: their greeting says how long it has been and what they did meanwhile, and their
        // prompt holds it a while (OwnLifeLine). Never into a scene or the minutes after one, at a player with nothing
        // on or asleep (mannersHold), nor while somebody else is talking with the player: they wait their turn, as every
        // visitor does. One greeting at a time.
        const auto clock = std::chrono::steady_clock::now();
        if (!IsBadMoment() && !mannersHold && clock - m_lastGreetAt >= kGreetGap) {
            for (const auto& life : m_lives) {
                if (life.flags & (LivingWorld::kWithPlayer | LivingWorld::kOnTask)) continue;
                const bool reunion = LivingWorld::ReunionOpen(life, now) && !LivingWorld::Has(life, LivingWorld::kReunionTold);
                if (!reunion && (life.steps == 0 || LivingWorld::Has(life, LivingWorld::kGreeted))) continue;
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
                if (!actor || actor->IsDead() || !NearPlayer(actor, player, kGreetDistance) || actor->IsInCombat()) continue;
                // Asleep they see no one: the greeting waits until they are up (else it was tried every tick all night).
                // In a scene of the game's or a quest's (an inn's chatter, a bard's song), they finish it first.
                if (Asleep(actor) || actor->GetCurrentScene()) continue;
                // Already talking (SkyrimNet's GameMaster or the player started it): that is their greeting, and a
                // "catches sight of" in the middle of it would start them over. The greeting order still runs, so a
                // meeting at a dungeon begins (2026-11-18: the GameMaster had Jenassa talk before IntelEngine greeted),
                // and a reunion's news is in their prompt all the same.
                const bool talking = TalkingNow(life.actor);
                if (!talking && OthersHaveTheEar(life.actor)) continue;
                m_order = Order{};
                m_order.kind = OrderKind::Greet;
                m_order.actor = life.actor;
                m_order.reunion = reunion;
                if (!talking) {
                    const std::string playerName = PlayerName();
                    if (reunion) {
                        Life meeting = life;   // apart until now (the player may have passed by at a distance first)
                        LivingWorld::ReunionTold(meeting, now);
                        m_order.narration = LivingWorld::ReunionNarration(meeting, NamesOf(life.actor, playerName));
                    }
                    if (m_order.narration.empty()) m_order.narration = Fill("{name} catches sight of {player}.", life, playerName);
                }
                m_orderAt = clock;   // (the gap to the next greeting counts from one that went out: Commit)
                return true;
            }
        }

        // A keepsake brought back for the player changes hands face to face, once the two have met again and stand
        // together (kKeepsakeReach): never across a room (nothing jumps into the player's pack from afar), never while
        // somebody else has the player's ear or their own line is still being said, and never on top of their greeting.
        if (!IsBadMoment() && !mannersHold && clock - m_lastGreetAt >= std::chrono::seconds(20)) {
            for (auto& life : m_lives) {
                if (!LivingWorld::MayHandOver(life, now)) continue;
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
                if (!actor || actor->IsDead() || actor->IsInCombat() || Asleep(actor) || actor->GetCurrentScene() ||
                    !NearPlayer(actor, player, LivingWorld::kKeepsakeReach)) {
                    continue;
                }
                if (SpeechTracker::GetSingleton()->IsSpeechPending(life.actor) || OthersHaveTheEar(life.actor)) continue;
                // Still in their pack? Asked of this one item only, once the two stand together.
                const RE::FormID id = life.keepsake;
                std::int32_t have = 0;
                if (RE::TESForm::LookupByID<RE::TESBoundObject>(id)) {
                    for (const auto& [object, count] : actor->GetInventoryCounts([id](RE::TESBoundObject& o) { return o.GetFormID() == id; })) {
                        if (object) have += count;
                    }
                }
                if (have <= 0) {
                    logger::info("LivingWorld: {} no longer carries what they kept for the player ({:08X}); there is nothing to hand over",
                                 GameText::In(actor->GetDisplayFullName()), id);
                    life.keepsake = 0;
                    life.keepsakeFrom = 0;
                    continue;
                }
                m_order = Order{};
                m_order.kind = OrderKind::Keepsake;
                m_order.actor = life.actor;
                m_order.item = id;
                m_order.narration = LivingWorld::KeepsakeNarration(life, NamesOf(life.actor, PlayerName()));
                m_orderAt = clock;
                return true;
            }
        }

        // One step of someone's own life, once SkyrimNet can take what they remember: right after a load it refuses
        // writes until its timeline check is done. A SkyrimNet whose memory never comes up does not stop lives for
        // good: after two minutes they go on, and the step's memory is left out (their prompt still tells their life).
        const auto sinceLoad = std::chrono::steady_clock::now() - m_loadedAt;
        if (sinceLoad < std::chrono::seconds(20)) return false;
        const bool waitForMemory = !writable && sinceLoad < std::chrono::seconds(120);
        auto options = [](const std::vector<Spot>& spots) {
            std::vector<LivingWorld::PlaceOption> list;
            list.reserve(spots.size());
            for (const auto& s : spots) list.push_back({s.location, s.hold});
            return list;
        };
        const auto inns = options(m_inns), dungeons = options(m_dungeons), towns = options(m_towns);
        auto mayGo = [&](const Life& life) {
            if (!LivingWorld::LifeMayBegin(life, settings, now) || !LivingWorld::MayRelocate(life, settings, false)) return false;
            if (life.flags & LivingWorld::kNeedsRelease) return false;
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
            return actor && !actor->IsDead() && !actor->IsPlayerTeammate() && !PlayerCouldSee(actor, player);
        };
        // Why a former follower's own life does not move on now, logged when it changes (players' logs say why
        // nothing happens). Empty when it may.
        auto whyNot = [&](const Life& life) -> std::string {
            if (waitForMemory) return "SkyrimNet is not ready to remember yet";
            if (life.flags & LivingWorld::kNeedsRelease) return "the last step is being undone";
            if (LivingWorld::Has(life, LivingWorld::kOnTask)) return "away on one of the player's errands";
            if (LivingWorld::Has(life, LivingWorld::kWaitOrder) && life.activity == Activity::Waiting) return "still waiting for the player";
            if (LivingWorld::PinnedByReality(life)) return "near the player or seen within the last hours";
            if (LivingWorld::Has(life, LivingWorld::kQuestTarget)) return "the target of an objective the player has open";
            if (LivingWorld::Has(life, LivingWorld::kKeepsShop)) return "keeps a shop or the player's home";
            if (LivingWorld::Has(life, LivingWorld::kQuestBound) && settings.questNpcMoves != LivingWorld::QuestNpcMoves::Freely) {
                return "takes part in a running quest";
            }
            const float apart = LivingWorld::Has(life, LivingWorld::kWaitOrder) ? settings.waitOrderDays : settings.lifeStartDays;
            if (now - life.parkedSince < apart) {
                return std::format("apart {:.1f} of {:.0f} days", now - life.parkedSince, apart);
            }
            if (now < life.nextChange) return std::format("the next change comes on day {:.1f}", life.nextChange);
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
            if (!actor || actor->IsDead()) return "not there";
            if (actor->IsPlayerTeammate()) return "serves the player";
            if (PlayerCouldSee(actor, player)) return "where the player could see";
            return {};
        };
        for (auto& life : m_lives) {
            if (!LivingWorld::Has(life, LivingWorld::kWasFollower) || life.parkedSince < 0.0f ||
                LivingWorld::Has(life, LivingWorld::kWithPlayer)) {
                continue;
            }
            const auto reason = whyNot(life);
            auto& said = m_whyNot[life.actor];
            if (reason != said) {
                if (!reason.empty()) {
                    auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
                    logger::info("LivingWorld: {}'s own life waits: {}", actor ? GameText::In(actor->GetDisplayFullName()) : "someone", reason);
                }
                said = reason;
            }
            if (!reason.empty() || !mayGo(life)) continue;
            RE::FormID hold = HoldIdOf(life.place);
            if (!hold) hold = HoldIdOf(life.parkedAt);
            if (!hold) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
                hold = actor ? HoldIdOf(LocationOf(actor)) : 0;
            }
            std::vector<Life> others;
            for (const auto& other : m_lives) {
                if (other.actor != life.actor && mayGo(other)) others.push_back(other);
            }
            const auto seed = LivingWorld::detail::Mix(life.actor, static_cast<std::uint32_t>(now * 24.0f));
            const auto step = LivingWorld::NextStep(life, hold, inns, dungeons, towns, others, now, seed);
            m_whyNot.erase(life.actor);
            Order order;
            order.actor = life.actor;
            order.step = step;
            order.companion = step.companion;
            if (step.activity == Activity::Home) {
                order.kind = OrderKind::GoHome;
            } else {
                const Spot* spot = FindSpot(step.place);
                auto* ref = spot ? RE::TESForm::LookupByID<RE::TESObjectREFR>(spot->ref) : nullptr;
                if (!ref || PlayerCouldSee(ref, player)) {
                    life.nextChange = now + 0.1f;  // try again in a couple of hours, maybe somewhere else
                    continue;
                }
                order.kind = OrderKind::Move;
                order.spot = *spot;
            }
            m_order = order;
            m_orderAt = std::chrono::steady_clock::now();
            return true;
        }
        return false;
    }

    void LivingWorldBridge::MarkGreeted(RE::FormID actor) {
        std::scoped_lock lock(m_mutex);
        for (auto& life : m_lives) {
            if (life.actor == actor) life.flags |= LivingWorld::kGreeted;
        }
    }

    std::string LivingWorldBridge::DebugAdventure(RE::Actor* actor) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!actor || !player) return {};
        std::scoped_lock lock(m_mutex);
        const bool known = std::any_of(m_lives.begin(), m_lives.end(), [actor](const Life& l) { return l.actor == actor->GetFormID(); });
        if (!known || m_order.kind != OrderKind::None) return {};
        const Spot* best = nullptr;
        float bestDistance = 0.0f;
        for (const auto& spot : m_dungeons) {
            auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(spot.ref);
            if (!ref || PlayerCouldSee(ref, player) || ref->GetWorldspace() != player->GetWorldspace()) continue;
            const float d = ref->GetPosition().GetDistance(player->GetPosition());
            if (!best || d < bestDistance) {
                best = &spot;
                bestDistance = d;
            }
        }
        if (!best) return {};
        Order order;
        order.kind = OrderKind::Move;
        order.actor = actor->GetFormID();
        order.spot = *best;
        order.step.activity = Activity::Adventuring;
        order.step.place = best->location;
        order.step.until = NowDays() + 2.0f;
        m_order = order;
        m_orderAt = std::chrono::steady_clock::now();
        auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(best->location);
        const std::string name = location && GameText::In(location->GetFullName()) ? GameText::In(location->GetFullName()) : "a dungeon";
        logger::info("LivingWorld: test: {} is sent adventuring at {} ({:.0f} units from the player, marker {:08X})", GameText::In(actor->GetDisplayFullName()), name, bestDistance, best->ref);
        return name;
    }

    RE::Actor* LivingWorldBridge::TakeOrder() {
        std::scoped_lock lock(m_mutex);
        if (m_order.kind == OrderKind::None) return nullptr;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(m_order.actor);
        if (!actor) m_order = Order{};
        return actor;
    }

    LivingWorldBridge::OrderKind LivingWorldBridge::CurrentOrderKind() const {
        std::scoped_lock lock(m_mutex);
        return m_order.kind;
    }

    RE::TESObjectREFR* LivingWorldBridge::OrderTarget() const {
        std::scoped_lock lock(m_mutex);
        return m_order.spot.ref ? RE::TESForm::LookupByID<RE::TESObjectREFR>(m_order.spot.ref) : nullptr;
    }

    float LivingWorldBridge::OrderOffset(int axis) const {
        std::scoped_lock lock(m_mutex);
        return axis == 0 ? m_order.spot.offset.x : axis == 1 ? m_order.spot.offset.y : axis == 2 ? m_order.spot.offset.z : 0.0f;
    }

    RE::Actor* LivingWorldBridge::OrderCompanion() const {
        std::scoped_lock lock(m_mutex);
        return m_order.companion ? RE::TESForm::LookupByID<RE::Actor>(m_order.companion) : nullptr;
    }

    bool LivingWorldBridge::OrderProtect() const {
        std::scoped_lock lock(m_mutex);
        return m_order.kind == OrderKind::Move && m_order.step.activity == Activity::Adventuring;
    }

    std::string LivingWorldBridge::OrderNarration() const {
        std::scoped_lock lock(m_mutex);
        return m_order.narration;
    }

    RE::TESForm* LivingWorldBridge::OrderItem() const {
        std::scoped_lock lock(m_mutex);
        return m_order.kind == OrderKind::Keepsake && m_order.item ? RE::TESForm::LookupByID(m_order.item) : nullptr;
    }

    void LivingWorldBridge::OrderDone(bool ok) {
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        if (m_order.kind == OrderKind::None) return;
        if (ok) {
            Commit(m_order, now);
        } else if (m_order.kind == OrderKind::Move || m_order.kind == OrderKind::GoHome) {
            for (auto& life : m_lives) {
                if (life.actor == m_order.actor) life.nextChange = now + 0.1f;
            }
        }
        m_order = Order{};
    }

    // ---------------------------------------------------------------- making it true

    void LivingWorldBridge::Commit(const Order& order, float now) {
        auto find = [this](RE::FormID id) -> Life* {
            for (auto& l : m_lives) {
                if (l.actor == id) return &l;
            }
            return nullptr;
        };
        Life* life = find(order.actor);
        if (!life) return;
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(order.actor);
        const char* name = actor ? GameText::In(actor->GetDisplayFullName()) : "someone";
        switch (order.kind) {
            case OrderKind::Release: {
                const bool teammate = actor && actor->IsPlayerTeammate();
                const bool errand = (life->flags & LivingWorld::kOnTask) != 0;
                const bool waiting = teammate && (life->flags & LivingWorld::kWaitOrder);
                logger::info("LivingWorld: {}'s own life ends for now ({})", name,
                             teammate ? "back with the player" : errand ? "an errand of the player's" : "a story or the settings");
                life->activity = teammate ? (waiting ? Activity::Waiting : Activity::WithPlayer) : errand ? Activity::OnTask : Activity::Home;
                life->place = 0;
                life->companion = 0;
                life->flags &= ~(LivingWorld::kNeedsRelease | LivingWorld::kGreeted);
                life->nextChange = std::max(life->nextChange, now + 1.0f);
                if (teammate && !waiting) {
                    life->parkedSince = -1.0f;
                    life->parkedAt = 0;
                    life->steps = 0;
                }
                DropRumor(*life);
                for (auto& other : m_lives) {
                    if (other.companion == order.actor) other.companion = 0;
                }
                return;
            }
            case OrderKind::Greet:
                life->flags |= LivingWorld::kGreeted;
                m_lastGreetAt = std::chrono::steady_clock::now();
                if (order.reunion) {
                    LivingWorld::ReunionTold(*life, now);
                    logger::info("LivingWorld: {} noticed the player again, after {} apart ({})", name, LivingWorld::Span(life->apartDays),
                                 order.narration.empty() ? "already talking: what passed is in their prompt" : order.narration);
                } else {
                    logger::info("LivingWorld: {} noticed the player", name);
                }
                return;
            case OrderKind::Keepsake: {
                const std::string memory = LivingWorld::KeepsakeMemory(*life, NamesOf(order.actor, PlayerName()));
                logger::info("LivingWorld: {} put {} into the player's hand", name, ItemName(life->keepsake));
                m_lastGreetAt = std::chrono::steady_clock::now();
                life->keepsake = 0;
                life->keepsakeFrom = 0;
                if (!memory.empty() && SkyrimNetAPI::AddMemory) {
                    const RE::FormID who = order.actor;
                    std::thread([who, memory]() {
                        const int id = SkyrimNetAPI::AddMemory(who, memory.c_str(), 0.6f, "EXPERIENCE", "", "", "[\"intelengine_life\"]", "[]");
                        logger::info("LivingWorld: memory {} for {:08X}: {}", id, who, memory);
                    }).detach();
                }
                return;
            }
            case OrderKind::Move:
            case OrderKind::GoHome: {
                const bool hadOwnLife = LivingWorld::OwnLife(*life);
                const bool firstStep = life->steps == 0;
                Order written = order;
                written.narration.clear();
                // Where they set out from (their last place, else where the player left them), and their own home.
                written.origin = hadOwnLife && life->place ? life->place : life->parkedAt;
                written.home = HomeOf(actor);
                // Back from an adventure of their own: how it ended is decided now and made true at once (what they
                // found goes into their pack, a keepsake is set aside for the player, the rest they remember).
                if (hadOwnLife && life->activity == Activity::Adventuring && life->place) {
                    LivingWorld::PastStep* adventure = LivingWorld::LastStep(*life);
                    if (!adventure || adventure->activity != Activity::Adventuring || adventure->place != life->place || adventure->over) {
                        LivingWorld::PastStep began;   // (a life of a save from before the steps were kept)
                        began.activity = Activity::Adventuring;
                        began.place = life->place;
                        began.companion = life->companion;
                        began.at = life->stepAt;
                        LivingWorld::Remember(*life, began);
                        adventure = LivingWorld::LastStep(*life);
                    }
                    int forced = -1;
                    if (const auto it = m_forcedReturn.find(order.actor); it != m_forcedReturn.end()) {
                        forced = it->second;
                        m_forcedReturn.erase(it);
                    }
                    adventure->over = true;
                    adventure->back = LivingWorld::ComeBack(LivingWorld::detail::Mix(order.actor, static_cast<std::uint32_t>(now * 24.0f)), forced);
                    adventure->find = adventure->back.find ? SpoilsItem(adventure->back.gem) : 0;
                    if (!adventure->find) {   // no such item in this game: nothing was found
                        adventure->back.find = false;
                        adventure->back.keepsake = false;
                        adventure->back.gold = 0;
                    }
                    if (adventure->back.keepsake) {
                        life->keepsake = adventure->find;
                        life->keepsakeFrom = adventure->place;
                    }
                    Pack(order.actor, adventure->find, adventure->back.gold);
                    written.cameBack = true;
                    written.back = *adventure;
                    logger::info("LivingWorld: {} came back from '{}': found {} ({} gold), hurt {}, kept for the player {}", name,
                                 PlaceName(adventure->place), adventure->find ? ItemName(adventure->find) : std::string("nothing"),
                                 adventure->back.gold, adventure->back.hurt, adventure->back.keepsake);
                }
                // Whoever went along with them last time goes their own way now too.
                for (auto& other : m_lives) {
                    if (other.companion == order.actor && other.actor != order.companion) {
                        other.companion = 0;
                        other.nextChange = std::min(other.nextChange, now);
                    }
                }
                // Going home from home is nothing anyone remembers (nor tells when they meet again).
                const bool remember = order.kind == OrderKind::Move || hadOwnLife || firstStep;
                auto apply = [&](Life& l, RE::FormID partner, bool told) {
                    l.activity = order.step.activity;
                    l.place = order.step.place;
                    l.companion = partner;
                    l.nextChange = order.step.until;
                    l.stepAt = now;
                    l.steps = static_cast<std::uint16_t>(std::min<int>(l.steps + 1, 60000));
                    l.flags &= ~LivingWorld::kGreeted;
                    DropRumor(l);
                    if (told) {
                        LivingWorld::PastStep step;
                        step.activity = order.step.activity;
                        step.place = order.step.place;
                        step.companion = partner;
                        step.at = now;
                        LivingWorld::Remember(l, step);
                    }
                };
                apply(*life, order.companion, remember);
                if (order.companion) {
                    if (Life* partner = find(order.companion)) apply(*partner, order.actor, true);
                }
                if (order.step.activity == Activity::Adventuring) {
                    for (const auto id : {order.actor, order.companion}) {
                        auto* who = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
                        auto* base = who ? who->GetActorBase() : nullptr;
                        if (base) {
                            logger::info("LivingWorld: {} is away at a dungeon: essential {}, protected {}", GameText::In(who->GetDisplayFullName()),
                                         base->IsEssential(), base->IsProtected());
                        }
                    }
                }
                logger::info("LivingWorld: {} {} '{}'{} until day {:.1f}", name,
                             order.step.activity == Activity::Working       ? "takes work at"
                             : order.step.activity == Activity::Adventuring ? "sets out for"
                             : order.step.activity == Activity::Travelling  ? "travels to"
                                                                            : "goes home",
                             PlaceName(order.step.place), order.companion ? " with a companion" : "", order.step.until);
                if (remember) WriteStep(written);
                return;
            }
            default:
                return;
        }
    }

    std::string LivingWorldBridge::Fill(std::string text, const Life& life, const std::string& player) const {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
        auto* companion = life.companion ? RE::TESForm::LookupByID<RE::Actor>(life.companion) : nullptr;
        const auto parked = PlaceName(life.parkedAt);
        if (parked.empty()) Replace(text, " at {parked}", "");
        Replace(text, "{parked}", parked);
        Replace(text, "{player}", player);
        Replace(text, "{place}", PlaceName(life.place));
        Replace(text, "{companion}", companion ? Safe::Name(GameText::In(companion->GetDisplayFullName()), "a companion") : "a companion");
        Replace(text, "{name}", actor ? Safe::Name(GameText::In(actor->GetDisplayFullName()), "someone") : "someone");
        Replace(text, "{ago}", LivingWorld::Ago(NowDays() - life.parkedSince));
        return text;
    }

    void LivingWorldBridge::WriteStep(const Order& order) {
        // m_mutex is held: gather the texts now, write them on a worker (SkyrimNet embeds and stores them).
        auto* playerRef = RE::PlayerCharacter::GetSingleton();
        const std::string player = playerRef ? Safe::Name(GameText::In(playerRef->GetDisplayFullName())) : "";
        struct Write {
            RE::FormID actor = 0;
            std::string memory;
            std::string related;
        };
        std::vector<Write> memories;
        std::string rumor, condition, label;
        const Life* life = nullptr;
        for (const auto& l : m_lives) {
            if (l.actor == order.actor) life = &l;
        }
        if (!life) return;
        const bool together = order.companion != 0;
        // steps was counted already: the first step is the one that made it 1.
        const bool firstStep = life->steps == 1;
        const auto names = NamesOf(order.actor, player);
        // Back from an adventure of their own: how it went, before the step that follows.
        if (order.cameBack) {
            if (auto memory = LivingWorld::ReturnMemory(order.back, names); !memory.empty()) {
                memories.push_back({order.actor, std::move(memory), order.back.companion ? "[" + std::to_string(order.back.companion) + "]" : "[]"});
            }
        }
        memories.push_back({order.actor, Fill(LivingWorld::StepMemory(order.step.activity, firstStep, together), *life, player),
                            together ? "[" + std::to_string(order.companion) + "]" : "[]"});
        if (together) {
            for (const auto& l : m_lives) {
                if (l.actor != order.companion) continue;
                memories.push_back({l.actor, Fill(LivingWorld::StepMemory(order.step.activity, l.steps == 1, true), l, player),
                                    "[" + std::to_string(order.actor) + "]"});
            }
        }
        const char* rumorTemplate = LivingWorld::StepRumor(order.step.activity, together);
        if (*rumorTemplate && SkyrimNetAPI::AddWorldKnowledge) {
            rumor = Fill(rumorTemplate, *life, player);
            // People of the hold they went to and of the hold where they parted heard of it, not the whole of Skyrim.
            std::vector<std::string> holds;
            for (const auto location : {HoldIdOf(order.step.place), HoldIdOf(life->parkedAt)}) {
                const auto holdName = LocationName(location);
                if (!holdName.empty() && std::find(holds.begin(), holds.end(), holdName) == holds.end()) holds.push_back(holdName);
            }
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(order.actor);
            const std::string name = actor ? Safe::Name(GameText::In(actor->GetDisplayFullName())) : "";
            for (std::size_t i = 0; i < holds.size(); ++i) {
                condition += (i ? " or " : "(");
                condition += "get_intelengine_hold(actorUUID) == \"" + Escape(holds[i]) + "\"";
            }
            if (!condition.empty()) condition += ") and actorName != \"" + Escape(name) + "\"";
            label = std::string(kRumorLabel) + name;
        }
        const auto placeName = PlaceName(order.step.place);
        const float stepAt = life->stepAt;
        const RE::FormID owner = order.actor;
        // The talk of the day as well (RumorBridge): each new step of their life replaces the word of the last. It is
        // hearsay of what people could see (where they work, that they set out armed, that they came back bandaged or
        // with something to show for it), begins where it was seen, carries with its weight to the holds around (a
        // publican there brings it up to the player) and is forgotten as other talk is.
        LivingWorld::Told told;
        told.activity = order.step.activity;
        told.place = order.step.place;
        told.companion = order.companion;
        told.firstStep = firstStep;
        told.back = order.cameBack ? &order.back : nullptr;
        told.origin = order.origin;
        told.home = order.home;
        const auto talk = LivingWorld::TalkOf(told, names);
        RumorBridge::GetSingleton()->Withdraw(Rumors::Owner::Life, owner);
        if (!talk.text.empty() && talk.where) {
            // Where this life wrote no entry of its own for the hold the talk begins in (going home, setting out from
            // another hold), the ledger writes the hold's entry itself.
            const RE::FormID talkHold = HoldIdOf(talk.where);
            const bool covered = !rumor.empty() && talkHold && (talkHold == HoldIdOf(order.step.place) || talkHold == HoldIdOf(life->parkedAt));
            RumorBridge::GetSingleton()->Note(Rumors::Kind::Life, talk.text, talk.where, owner, 0, Rumors::Owner::Life, owner, talk.weight, !covered);
        }
        std::thread([this, memories, rumor, condition, label, placeName, stepAt, owner]() {
            for (const auto& m : memories) {
                if (m.memory.empty() || !SkyrimNetAPI::AddMemory) continue;
                const int id = SkyrimNetAPI::AddMemory(m.actor, m.memory.c_str(), 0.55f, "EXPERIENCE", "", placeName.c_str(),
                                                       "[\"intelengine_life\"]", m.related.c_str());
                logger::info("LivingWorld: memory {} for {:08X}: {}", id, m.actor, m.memory);
            }
            if (rumor.empty() || condition.empty()) return;
            const int id = SkyrimNetAPI::AddWorldKnowledge(rumor.c_str(), condition.c_str(), false, 0.5f, label.c_str());
            logger::info("LivingWorld: rumor {}: {} (heard where {})", id, rumor, condition);
            if (id <= 0) return;
            bool kept = false;
            {
                std::scoped_lock lock(m_mutex);
                for (auto& l : m_lives) {
                    if (l.actor == owner && l.stepAt == stepAt && l.rumor == 0) {
                        l.rumor = id;
                        kept = true;
                    }
                }
            }
            // Their life moved on while this was written: what people heard is already out of date.
            if (!kept && SkyrimNetAPI::RemoveWorldKnowledge) SkyrimNetAPI::RemoveWorldKnowledge(id);
        }).detach();
    }

    void LivingWorldBridge::DropRumor(Life& life) {
        const int id = life.rumor;
        life.rumor = 0;
        if (id <= 0 || !SkyrimNetAPI::RemoveWorldKnowledge) return;
        std::thread([id]() {
            const bool removed = SkyrimNetAPI::RemoveWorldKnowledge(id);
            logger::info("LivingWorld: rumor {} {}", id, removed ? "withdrawn" : "was already gone");
        }).detach();
    }

    void LivingWorldBridge::Reconcile() {
        // m_mutex is held. A rumor of ours that this save's record does not know (written after this save, or left by
        // a game that crashed before saving) would tell of a life that did not happen here: withdraw it.
        if (!SkyrimNetAPI::GetWorldKnowledge || !SkyrimNetAPI::RemoveWorldKnowledge) return;
        std::unordered_set<int> known;
        for (const auto& l : m_lives) {
            if (l.rumor > 0) known.insert(l.rumor);
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
            logger::info("LivingWorld: {} rumor(s) of other timelines withdrawn, {} kept", withdrawn, known.size());
        }).detach();
    }

    // ---------------------------------------------------------------- what prompts are told

    std::string LivingWorldBridge::OwnLifeLine(RE::Actor* actor) const {
        if (!actor) return {};
        std::scoped_lock lock(m_mutex);
        if (m_settings.level == LivingWorld::WorldLevel::Classic) return {};
        for (const auto& life : m_lives) {
            if (life.actor != actor->GetFormID()) continue;
            const std::string player = PlayerName();
            // Meeting the player again after days apart: what passed, while it is theirs to tell (LivingWorld.h).
            const bool lives = m_settings.level == LivingWorld::WorldLevel::Lives;
            // Only while the player is about (the last look's): talking with anyone else they are not meeting the player.
            if (const float now = NowDays(); lives && LivingWorld::Has(life, LivingWorld::kNearPlayer) && LivingWorld::ReunionOpen(life, now)) {
                Life view = life;   // not greeted yet: apart until now
                if (!LivingWorld::Has(view, LivingWorld::kReunionTold)) LivingWorld::ReunionTold(view, now);
                if (auto block = LivingWorld::ReunionBlock(view, NamesOf(life.actor, player)); !block.empty()) return block;
            }
            // A keepsake still carried for the player stays theirs to mention until it changes hands.
            const std::string kept = lives ? LivingWorld::KeepsakeLine(life, NamesOf(life.actor, player)) : std::string();
            const char* line = LivingWorld::OwnLifeLine(life);
            if (!*line) return kept.empty() ? std::string() : "## Kept for " + player + "\n" + kept;
            return "## My own life lately\n" + Fill(line, life, player) + (kept.empty() ? std::string() : "\n" + kept);
        }
        return {};
    }

    bool LivingWorldBridge::HasOwnLife(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        if (m_settings.level != LivingWorld::WorldLevel::Lives) return false;
        for (const auto& life : m_lives) {
            if (life.actor == actor) {
                return LivingWorld::Has(life, LivingWorld::kWasFollower) && life.parkedSince >= 0.0f &&
                       !LivingWorld::Has(life, LivingWorld::kWithPlayer) && !LivingWorld::Has(life, LivingWorld::kOnTask);
            }
        }
        return false;
    }

    bool LivingWorldBridge::FreeToTravel(RE::Actor* actor) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !Person(actor) || actor->IsDisabled() || actor->IsPlayerTeammate() || actor->GetCurrentScene()) return false;
        if (actor->GetVendorFaction() || actor->IsInCombat()) return false;
        if (NearPlayer(actor, player, 6000.0f)) return false;   // never taken from where the player could see them go
        const auto ties = TiesOf(actor);
        return !ties.errand && !ties.journal && !ties.quest && !ties.home;
    }

    std::pair<RE::FormID, RE::FormID> LivingWorldBridge::AdventuringAt(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        for (const auto& life : m_lives) {
            if (life.actor == actor && life.activity == Activity::Adventuring && life.place) return {life.place, life.companion};
        }
        return {0, 0};
    }

    RE::FormID LivingWorldBridge::FreeCompanionFor(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        for (const auto& other : m_lives) {
            const bool free = LivingWorld::Has(other, LivingWorld::kWasFollower) && other.actor != actor &&
                              !LivingWorld::PinnedByReality(other) && !LivingWorld::Has(other, LivingWorld::kQuestTarget) &&
                              !LivingWorld::Has(other, LivingWorld::kQuestBound) && !LivingWorld::Has(other, LivingWorld::kKeepsShop) &&
                              (other.activity == Activity::Home || other.activity == Activity::Working) && other.companion == 0 &&
                              other.parkedSince >= 0.0f;
            if (!free) continue;
            auto* a = RE::TESForm::LookupByID<RE::Actor>(other.actor);
            if (a && !a->IsDead() && !a->IsDisabled()) return other.actor;
        }
        return 0;
    }

    bool LivingWorldBridge::CloseToPlayer(RE::Actor* actor) const {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!actor || !player) return false;
        {
            std::scoped_lock lock(m_mutex);
            for (const auto& life : m_lives) {
                if (life.actor == actor->GetFormID() && (life.flags & LivingWorld::kWasFollower)) return true;
            }
        }
        auto* npc = actor->GetActorBase();
        auto* pc = player->GetActorBase();
        if (npc && pc) {
            if (auto* tie = RE::BGSRelationship::GetRelationship(npc, pc)) {
                using L = RE::BGSRelationship::RELATIONSHIP_LEVEL;
                const auto level = tie->level.get();
                if (level == L::kLover || level == L::kAlly || level == L::kConfidant) return true;
            }
        }
        // The two factions are looked up once: asked for by a plugin's name the game walks its whole load order, and the
        // pacer asks this of every person of a town in one frame.
        static std::once_flag looked;
        static RE::TESFaction* married = nullptr;
        static RE::TESFaction* bond = nullptr;
        auto* data = RE::TESDataHandler::GetSingleton();
        if (!data) return false;
        std::call_once(looked, [data]() {
            married = data->LookupForm<RE::TESFaction>(0x0C6472, "Skyrim.esm");
            bond = data->LookupForm<RE::TESFaction>(0xD63, "SNRom_Integration.esp");
        });
        // Wed to the player (the game's own faction; SkyrimNet Relationships adds its spouses to it too).
        if (married && actor->IsInFaction(married)) return true;
        // SkyrimNet Relationships, when installed: a Confidant or more (its bond faction's rank; its README invites the read).
        if (bond) {
            bool member = false;
            std::int8_t rank = -1;
            actor->VisitFactions([&](RE::TESFaction* faction, std::int8_t r) {
                if (faction != bond) return false;
                member = true;
                rank = r;
                return true;
            });
            if (member && rank >= 3) return true;
        }
        // What was read from their memories of the player, where that is IntelEngine's to say for them (Bonds::See: no
        // relationship mod, or one that does not watch them, or one that took them on and has not caught up yet): a
        // confidant or more, and no ill will.
        const Bonds::View read = BondBridge::GetSingleton()->Seen(actor);
        return read.sour == Bonds::NotSour && read.standing >= Bonds::Confidant;
    }

    std::vector<LivingWorldBridge::OldCompanion> LivingWorldBridge::OldCompanions() const {
        std::vector<OldCompanion> out;
        std::vector<std::pair<RE::FormID, RE::FormID>> lives;
        {
            std::scoped_lock lock(m_mutex);
            if (m_settings.level != LivingWorld::WorldLevel::Lives) return out;
            for (const auto& life : m_lives) {
                if (!LivingWorld::OwnLife(life)) continue;
                if (life.flags & (LivingWorld::kWithPlayer | LivingWorld::kOnTask | LivingWorld::kNeedsRelease)) continue;
                lives.emplace_back(life.actor, life.place);
            }
        }
        for (const auto& [id, place] : lives) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor || actor->IsDead() || actor->IsPlayerTeammate() || QuestTie(actor) != 0) continue;
            if (QuestBridge::GetSingleton()->Involves(id) || ArcBridge::GetSingleton()->Involves(id)) continue;
            out.push_back({id, place, DirectorLifeLine(id)});
        }
        return out;
    }

    std::string LivingWorldBridge::DirectorLifeLine(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        if (m_settings.level == LivingWorld::WorldLevel::Classic) return {};
        for (const auto& life : m_lives) {
            if (life.actor != actor) continue;
            const char* line = LivingWorld::DirectorLifeLine(life);
            if (!*line) return {};
            auto* player = RE::PlayerCharacter::GetSingleton();
            return Fill(line, life, player ? Safe::Name(GameText::In(player->GetDisplayFullName())) : "");
        }
        return {};
    }

    std::string LivingWorldBridge::DashboardJson() const {
        std::vector<std::pair<RE::FormID, std::uint16_t>> people;
        {
            std::scoped_lock lock(m_mutex);
            for (const auto& life : m_lives) {
                if (life.flags & (LivingWorld::kWithPlayer | LivingWorld::kWasFollower)) people.emplace_back(life.actor, life.flags);
            }
        }
        nlohmann::json out = nlohmann::json::array();
        for (const auto& [id, flags] : people) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor) continue;
            nlohmann::json j;
            j["name"] = Safe::Name(GameText::In(actor->GetDisplayFullName()));
            j["with_you"] = (flags & LivingWorld::kWithPlayer) != 0 && (flags & LivingWorld::kWaitOrder) == 0;
            j["waiting"] = (flags & LivingWorld::kWaitOrder) != 0;
            j["dead"] = actor->IsDead();
            j["line"] = DirectorLifeLine(id);   // takes the lock itself
            out.push_back(std::move(j));
        }
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string LivingWorldBridge::Describe(RE::Actor* actor) const {
        if (!actor) return "no actor";
        std::scoped_lock lock(m_mutex);
        for (const auto& l : m_lives) {
            if (l.actor != actor->GetFormID()) continue;
            const float now = NowDays();
            std::string past;
            for (std::uint8_t k = 0; k < l.pastCount; ++k) {
                const auto& s = l.past[k];
                past += std::format("{}{}@'{}' day {:.1f}{}", k ? ", " : "", static_cast<int>(s.activity), PlaceName(s.place), s.at,
                                    s.over ? std::format(" (back: find {} {}g hurt {} kept {})", ItemName(s.find), s.back.gold, s.back.hurt, s.back.keepsake)
                                           : std::string());
            }
            return std::format("flags={:#06x} activity={} place='{}' companion={:08X} parkedSince={:.2f} parkedAt='{}' steps={} "
                               "nextChange={:.2f} stepAt={:.2f} rumor={} lastMet={:.2f} now={:.2f} reunion={} (since {:.2f}, apart {:.1f} days, told {}) "
                               "keepsake='{}' from '{}' past=[{}]",
                               l.flags, static_cast<int>(l.activity), PlaceName(l.place), l.companion, l.parkedSince,
                               PlaceName(l.parkedAt), l.steps, l.nextChange, l.stepAt, l.rumor, l.lastMet, now,
                               LivingWorld::ReunionOpen(l, now) ? "open" : "none", l.metAgainAt, l.apartDays,
                               LivingWorld::Has(l, LivingWorld::kReunionTold), ItemName(l.keepsake), PlaceName(l.keepsakeFrom), past);
        }
        return "no record";
    }

    // ---------------------------------------------------------------- test hooks and the lives' own natives

    std::string LivingWorldBridge::DebugApart(RE::Actor* actor, float days) {
        if (!actor) return "no actor";
        const float now = NowDays();
        days = std::clamp(days, LivingWorld::kReunionGapDays, 365.0f);
        std::scoped_lock lock(m_mutex);
        for (auto& life : m_lives) {
            if (life.actor != actor->GetFormID()) continue;
            if (!LivingWorld::Has(life, LivingWorld::kWasFollower)) return "has never followed the player: there is no life of theirs to tell";
            if (actor->IsPlayerTeammate()) return "follows the player now: dismiss them first";
            life.lastMet = now - days;
            if (life.parkedSince < 0.0f || life.parkedSince > life.lastMet) {
                life.parkedSince = life.lastMet;
                if (!life.parkedAt) life.parkedAt = LocationOf(actor);
            }
            life.metAgainAt = -1.0f;
            life.flags &= ~(LivingWorld::kReunionTold | LivingWorld::kNearPlayer | LivingWorld::kSeenRecently);
            logger::info("LivingWorld: test: the player was last near {} {} ago", GameText::In(actor->GetDisplayFullName()), LivingWorld::Span(days));
            return "the player was last near them " + LivingWorld::Span(days) + " ago: the next look that finds the player near is a reunion";
        }
        return "no record of them";
    }

    std::string LivingWorldBridge::DebugComeBack(RE::Actor* actor, int outcome) {
        if (!actor) return "no actor";
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        for (auto& life : m_lives) {
            if (life.actor != actor->GetFormID()) continue;
            if (life.activity != Activity::Adventuring || !life.place) {
                return "is on no adventure of their own (IntelEngine_Quests.DebugAdventure sends them on one)";
            }
            // 0 by chance; else bits 1 a find, 2 hurt, 4 the find kept for the player (8 alone: back with nothing, unhurt).
            m_forcedReturn[life.actor] = outcome <= 0 ? -1 : (outcome & 7);
            life.nextChange = now;
            logger::info("LivingWorld: test: {}'s adventure at '{}' ends at the next look nobody sees (outcome {})",
                         GameText::In(actor->GetDisplayFullName()), PlaceName(life.place), outcome);
            return "their adventure at " + PlaceName(life.place) + " ends at the next look the player cannot see them";
        }
        return "no record of them";
    }

    std::string LivingWorldBridge::DebugKeepsake(RE::Actor* actor) {
        if (!actor) return "no actor";
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        for (auto& life : m_lives) {
            if (life.actor != actor->GetFormID()) continue;
            if (!LivingWorld::Has(life, LivingWorld::kWasFollower)) return "has never followed the player";
            const RE::FormID item = SpoilsItem(static_cast<std::uint8_t>(LivingWorld::detail::Mix(life.actor, static_cast<std::uint32_t>(now * 24.0f))));
            if (!item) return "this game has none of the gems a find is made of";
            life.keepsake = item;
            life.keepsakeFrom = life.place ? life.place : life.parkedAt ? life.parkedAt : LocationOf(actor);
            life.flags |= LivingWorld::kGreeted;   // as if they had met the player on this step already (MayHandOver)
            Pack(life.actor, item, 0);
            logger::info("LivingWorld: test: {} carries {} for the player; it changes hands within {:.0f} units", GameText::In(actor->GetDisplayFullName()),
                         ItemName(item), LivingWorld::kKeepsakeReach);
            return "carries " + ItemName(item) + " for the player: walk up to them";
        }
        return "no record of them";
    }

    namespace {
        RE::TESForm* LivesOrderItem(RE::StaticFunctionTag*) { return LivingWorldBridge::GetSingleton()->OrderItem(); }
        RE::BSFixedString LivesDebugApart(RE::StaticFunctionTag*, RE::Actor* actor, float days) {
            return RE::BSFixedString(LivingWorldBridge::GetSingleton()->DebugApart(actor, days).c_str());
        }
        RE::BSFixedString LivesDebugComeBack(RE::StaticFunctionTag*, RE::Actor* actor, std::int32_t outcome) {
            return RE::BSFixedString(LivingWorldBridge::GetSingleton()->DebugComeBack(actor, outcome).c_str());
        }
        RE::BSFixedString LivesDebugKeepsake(RE::StaticFunctionTag*, RE::Actor* actor) {
            return RE::BSFixedString(LivingWorldBridge::GetSingleton()->DebugKeepsake(actor).c_str());
        }
    }

    void LivingWorldBridge::RegisterNatives(RE::BSScript::IVirtualMachine* a_vm, int& count) {
        constexpr const char* script = "IntelEngine_Lives";
        a_vm->RegisterFunction("LivesOrderItem", script, LivesOrderItem); ++count;
        a_vm->RegisterFunction("LivesDebugApart", script, LivesDebugApart); ++count;
        a_vm->RegisterFunction("LivesDebugComeBack", script, LivesDebugComeBack); ++count;
        a_vm->RegisterFunction("LivesDebugKeepsake", script, LivesDebugKeepsake); ++count;
    }

    int LivingWorldBridge::QuestTie(RE::Actor* actor, bool skipStorylines) {
        if (!actor) return 0;
        const auto ties = TiesOf(actor, skipStorylines);
        return ties.journal ? 2 : ties.quest ? 1 : 0;
    }

    std::string LivingWorldBridge::HoldName(RE::Actor* actor) {
        auto* hold = actor ? HoldOf(actor->GetCurrentLocation()) : nullptr;
        const char* name = hold ? GameText::In(hold->GetFullName()) : nullptr;
        return name ? name : "";
    }
}
