#include "LivingWorldBridge.h"
#include "QuestBridge.h"

#include "ProcessUtils.h"
#include "SkyrimNetAPI.h"

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
            ~FrameCost() {
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                if (ms >= noticeMs) logger::info("LivingWorld: {} took {:.1f} ms of the game's frame", what, ms);
                // And how long it takes as a rule: the slowest of every 30, now and then.
                static int runs = 0;
                static double slowest = 0.0;
                slowest = std::max(slowest, ms);
                if (++runs >= 30) {
                    logger::info("LivingWorld: {}: at most {:.2f} ms of the game's frame in the last {} runs", what, slowest, runs);
                    runs = 0;
                    slowest = 0.0;
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
            const char* name = loc ? loc->GetFullName() : nullptr;
            return name ? name : "";
        }

        // A place's name as people say it in the middle of a sentence: an inn with its article and its town ("the
        // Sleeping Giant Inn in Riverwood", "the Bannered Mare in Whiterun"), a dungeon or town as it is named.
        std::string PlaceName(RE::FormID location) {
            auto* loc = location ? RE::TESForm::LookupByID<RE::BGSLocation>(location) : nullptr;
            const char* name = loc ? loc->GetFullName() : nullptr;
            if (!name || !*name) return "";
            std::string text = name;
            if (text.starts_with("The ")) {
                text[0] = 't';
            } else if (loc->HasKeywordString("LocTypeInn") && !text.starts_with("the ")) {
                text = "the " + text;
            }
            if (loc->HasKeywordString("LocTypeInn") && loc->parentLoc) {
                const char* town = loc->parentLoc->GetFullName();
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
            const char* name = location->GetFullName();
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
            const char* name = location->GetFullName();
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

        // Every loaded actor, all four process tiers: someone just moved in from an unloaded cell is not "high" yet.
        std::vector<RE::Actor*> serving;
        int loaded = 0;
        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            ++loaded;
            if (!actor->IsPlayerTeammate()) return false;
            if (Person(actor)) {
                serving.push_back(actor);
                return false;
            }
            // A teammate this does not count (a summon, a creature, a child): say why, once per actor and session.
            static std::unordered_set<RE::FormID> explained;
            if (explained.insert(actor->GetFormID()).second) {
                static auto* npcKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);
                auto* race = actor->GetRace();
                logger::info("LivingWorld: {} serves the player but has no life of their own here (dead {}, commanded {}, "
                             "child {}, person {})",
                             actor->GetDisplayFullName(), actor->IsDead(), actor->IsCommandedActor(),
                             race && race->data.flags.any(RE::RACE_DATA::Flag::kChild), npcKeyword && actor->HasKeyword(npcKeyword));
            }
            return false;
        });

        // Asked before the lock: SkyrimNet's threads call this bridge's decorators while they hold locks of their own.
        const bool writable = SkyrimNetWritable();
        const float now = NowDays();
        std::string followers;
        for (auto* actor : serving) {
            if (!followers.empty()) followers += ", ";
            followers += actor->GetDisplayFullName();
        }
        std::scoped_lock lock(m_mutex);
        if (followers != m_lastFollowers) {
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
            const bool teammate = actor->IsPlayerTeammate();
            const bool wasServing = (life.flags & LivingWorld::kWithPlayer) != 0;
            const bool wasAway = (life.flags & LivingWorld::kOnTask) != 0 || life.activity == Activity::OnTask;
            auto set = [&life](std::uint16_t flag, bool on) {
                life.flags = static_cast<std::uint16_t>(on ? (life.flags | flag) : (life.flags & ~flag));
            };
            const bool close = NearPlayer(actor, player, 6000.0f);
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
                        logger::info("LivingWorld: {} waits for the player at '{}'", actor->GetDisplayFullName(), LocationName(life.parkedAt));
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
                        logger::info("LivingWorld: the player left {} at '{}'", actor->GetDisplayFullName(), LocationName(life.parkedAt));
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
        PlanOne(now, settings, writable);
    }

    // ---------------------------------------------------------------- choosing the next change

    bool LivingWorldBridge::PlanOne(float now, const LivingWorld::Settings& settings, bool writable) {
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

        // The player comes upon someone living their own life: they notice, once per step of it.
        if (!IsBadMoment()) {
            for (const auto& life : m_lives) {
                if (life.steps == 0 || (life.flags & (LivingWorld::kGreeted | LivingWorld::kWithPlayer | LivingWorld::kOnTask))) continue;
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(life.actor);
                if (!actor || actor->IsDead() || !NearPlayer(actor, player, kGreetDistance) || actor->IsInCombat()) continue;
                // Asleep they see no one: the greeting waits until they are up (else it was tried every tick all night).
                if (auto* state = actor->AsActorState(); state && state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping) {
                    continue;
                }
                m_order = Order{};
                m_order.kind = OrderKind::Greet;
                m_order.actor = life.actor;
                m_order.narration = Fill("{name} catches sight of {player}.", life, player->GetDisplayFullName());
                m_orderAt = std::chrono::steady_clock::now();
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
                    logger::info("LivingWorld: {}'s own life waits: {}", actor ? actor->GetDisplayFullName() : "someone", reason);
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
        const char* name = actor ? actor->GetDisplayFullName() : "someone";
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
                logger::info("LivingWorld: {} noticed the player", name);
                return;
            case OrderKind::Move:
            case OrderKind::GoHome: {
                const bool hadOwnLife = LivingWorld::OwnLife(*life);
                const bool firstStep = life->steps == 0;
                // Whoever went along with them last time goes their own way now too.
                for (auto& other : m_lives) {
                    if (other.companion == order.actor && other.actor != order.companion) {
                        other.companion = 0;
                        other.nextChange = std::min(other.nextChange, now);
                    }
                }
                Order written = order;
                written.narration.clear();
                // Going home from home is nothing anyone remembers.
                const bool remember = order.kind == OrderKind::Move || hadOwnLife || firstStep;
                auto apply = [&](Life& l, RE::FormID partner) {
                    l.activity = order.step.activity;
                    l.place = order.step.place;
                    l.companion = partner;
                    l.nextChange = order.step.until;
                    l.stepAt = now;
                    l.steps = static_cast<std::uint16_t>(std::min<int>(l.steps + 1, 60000));
                    l.flags &= ~LivingWorld::kGreeted;
                    DropRumor(l);
                };
                apply(*life, order.companion);
                if (order.companion) {
                    if (Life* partner = find(order.companion)) apply(*partner, order.actor);
                }
                if (order.step.activity == Activity::Adventuring) {
                    for (const auto id : {order.actor, order.companion}) {
                        auto* who = id ? RE::TESForm::LookupByID<RE::Actor>(id) : nullptr;
                        auto* base = who ? who->GetActorBase() : nullptr;
                        if (base) {
                            logger::info("LivingWorld: {} is away at a dungeon: essential {}, protected {}", who->GetDisplayFullName(),
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
        Replace(text, "{companion}", companion ? companion->GetDisplayFullName() : "a companion");
        Replace(text, "{name}", actor ? actor->GetDisplayFullName() : "someone");
        Replace(text, "{ago}", LivingWorld::Ago(NowDays() - life.parkedSince));
        return text;
    }

    void LivingWorldBridge::WriteStep(const Order& order) {
        // m_mutex is held: gather the texts now, write them on a worker (SkyrimNet embeds and stores them).
        auto* playerRef = RE::PlayerCharacter::GetSingleton();
        const std::string player = playerRef ? playerRef->GetDisplayFullName() : "";
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
            const std::string name = actor ? actor->GetDisplayFullName() : "";
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
                const auto label = entry.value("display_name", std::string());
                const int id = entry.value("id", 0);
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
            const char* line = LivingWorld::OwnLifeLine(life);
            if (!*line) return {};
            auto* player = RE::PlayerCharacter::GetSingleton();
            return "## My own life lately\n" + Fill(line, life, player ? player->GetDisplayFullName() : "");
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

    std::string LivingWorldBridge::DirectorLifeLine(RE::FormID actor) const {
        std::scoped_lock lock(m_mutex);
        if (m_settings.level == LivingWorld::WorldLevel::Classic) return {};
        for (const auto& life : m_lives) {
            if (life.actor != actor) continue;
            const char* line = LivingWorld::DirectorLifeLine(life);
            if (!*line) return {};
            auto* player = RE::PlayerCharacter::GetSingleton();
            return Fill(line, life, player ? player->GetDisplayFullName() : "");
        }
        return {};
    }

    std::string LivingWorldBridge::Describe(RE::Actor* actor) const {
        if (!actor) return "no actor";
        std::scoped_lock lock(m_mutex);
        for (const auto& l : m_lives) {
            if (l.actor != actor->GetFormID()) continue;
            return std::format("flags={:#06x} activity={} place='{}' companion={:08X} parkedSince={:.2f} parkedAt='{}' steps={} "
                               "nextChange={:.2f} stepAt={:.2f} rumor={} lastMet={:.2f} now={:.2f}",
                               l.flags, static_cast<int>(l.activity), PlaceName(l.place), l.companion, l.parkedSince,
                               PlaceName(l.parkedAt), l.steps, l.nextChange, l.stepAt, l.rumor, l.lastMet, NowDays());
        }
        return "no record";
    }

    int LivingWorldBridge::QuestTie(RE::Actor* actor, bool skipStorylines) {
        if (!actor) return 0;
        const auto ties = TiesOf(actor, skipStorylines);
        return ties.journal ? 2 : ties.quest ? 1 : 0;
    }

    std::string LivingWorldBridge::HoldName(RE::Actor* actor) {
        auto* hold = actor ? HoldOf(actor->GetCurrentLocation()) : nullptr;
        const char* name = hold ? hold->GetFullName() : nullptr;
        return name ? name : "";
    }
}
