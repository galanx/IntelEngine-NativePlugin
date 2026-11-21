/**
 * BattleManager Implementation
 *
 * Tracks one active battle at a time. Papyrus drives spawning and cleanup;
 * C++ tracks actor state, morale, and generates poll events.
 */

#include "BattleManager.h"
#include "SoldierTemplates.h"
#include "Papyrus.h"
#include "LocationResolver.h"
#include "FactionPolitics.h"
#include "StringUtils.h"
#include "ProcessUtils.h"
#include "NPCIndex.h"
#include "PoliticalDB.h"
#include "FrameCost.h"
#include "Ranks.h"
#include "LivingWorldBridge.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace IntelEngine {

    // Named constants for battle tuning
    constexpr int MORALE_PENALTY_GENERIC = Battlefield::kLossSoldier;
    constexpr int MORALE_PENALTY_RECRUITED = Battlefield::kLossSoldier;
    constexpr int MORALE_PENALTY_LEADER = Battlefield::kLossCommander;
    constexpr int MORALE_BATTLE_END_THRESHOLD = Battlefield::kBreakAt;
    constexpr float WAVE_CASUALTY_THRESHOLD = 0.4f;
    constexpr int MORALE_PLAYER_JOIN_BOOST = Battlefield::kPlayerJoins;
    constexpr int MORALE_PLAYER_JOIN_ENEMY_PENALTY = Battlefield::kPlayerJoinsEnemy;

    namespace {
        constexpr float kDegPerRad = 180.f / Battlefield::kPi;

        // A word a script passes as a key arrives in whatever capitals the game first met that word in: "stranger"
        // came through as "Stranger" (the game has somebody of that name), matched nothing, and the killer's name
        // came out empty. Keys from scripts are read without regard to capitals.
        std::string Key(const std::string& word) {
            std::string key = word;
            for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return key;
        }
        bool IsA(const std::string& side) { return side == "A" || side == "a"; }

        void StripOwnFactions(RE::Actor* actor);        // further down, with the raising of soldiers

        double RealSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        // A faction's name inside a sentence: "the Imperial Legion", "the Stormcloaks".
        std::string The(const std::string& name) {
            if (name.rfind("The ", 0) == 0 || name.rfind("the ", 0) == 0) return "the " + name.substr(4);
            return "the " + name;
        }
        std::string CapThe(const std::string& name) {
            std::string s = The(name);
            s[0] = 'T';
            return s;
        }

        // The hold a place lies in, by the name the game gives it ("" when none is marked above it).
        std::string HoldNameOf(const RE::BGSLocation* location) {
            int depth = 0;
            for (auto* at = location; at && depth < 12; at = at->parentLoc, ++depth) {
                if (!at->HasKeywordString("LocTypeHold")) continue;
                if (const char* name = at->GetFullName(); name && name[0]) return name;
            }
            return {};
        }

        // Is this point on ground somebody can walk on: inside a triangle of the cell's navmesh, at about that height?
        // (Land is everywhere, also under the empty shell of a walled town as the world outside shows it and on a
        // mountain's face; the navmesh is where the game lets people go.)
        bool Walkable(RE::TESObjectCELL* cell, const RE::NiPoint3& at) {
            auto* meshes = cell ? cell->GetRuntimeData().navMeshes : nullptr;
            if (!meshes) return false;
            const auto side = [](const RE::NiPoint3& p, const RE::NiPoint3& a, const RE::NiPoint3& b) {
                return (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
            };
            for (const auto& mesh : meshes->navMeshes) {
                if (!mesh) continue;
                const auto& vertices = mesh->vertices;
                const auto count = vertices.size();
                for (const auto& triangle : mesh->triangles) {
                    const auto ia = triangle.vertices[0], ib = triangle.vertices[1], ic = triangle.vertices[2];
                    if (ia >= count || ib >= count || ic >= count) continue;
                    const auto& a = vertices[ia].location;
                    const auto& b = vertices[ib].location;
                    const auto& c = vertices[ic].location;
                    if (at.x < (std::min)({a.x, b.x, c.x}) || at.x > (std::max)({a.x, b.x, c.x}) || at.y < (std::min)({a.y, b.y, c.y}) ||
                        at.y > (std::max)({a.y, b.y, c.y})) {
                        continue;
                    }
                    const float d1 = side(at, a, b), d2 = side(at, b, c), d3 = side(at, c, a);
                    const bool negative = d1 < 0.f || d2 < 0.f || d3 < 0.f;
                    const bool positive = d1 > 0.f || d2 > 0.f || d3 > 0.f;
                    if (negative && positive) continue;
                    if (std::fabs((a.z + b.z + c.z) / 3.f - at.z) < 300.f) return true;
                }
            }
            return false;
        }

        // The ground at a point of the world around the player: is it there (loaded, out of doors), how high, under
        // water, and can it be walked on.
        struct Ground {
            bool there = false;
            float height = 0.f;
            bool wet = false;
            bool walkable = false;
        };
        Ground GroundAt(RE::TES* tes, Battlefield::Vec at, float nearZ) {
            Ground ground;
            const RE::NiPoint3 pos{at.x, at.y, nearZ};
            auto* cell = tes->GetCell(pos);
            if (!cell || cell->IsInteriorCell()) return ground;
            float height = 0.f;
            if (!tes->GetLandHeight(pos, height)) return ground;
            ground.there = true;
            ground.height = height;
            const float water = tes->GetWaterHeight(pos, cell);
            ground.wet = Battlefield::Wet(water, height);
            ground.walkable = Walkable(cell, RE::NiPoint3{at.x, at.y, height});
            return ground;
        }

        // A door into a house, a cave, a town within this distance: people live or lurk here. (A town in the open
        // has nobody out of doors at night, and a place's name in the game's data covers too much: all the plain
        // round Whiterun is "Whiterun".)
        bool DoorNear(RE::TES* tes, Battlefield::Vec centre, float radius) {
            auto* grid = tes->gridCells;
            const auto length = grid ? grid->length : 0;
            bool found = false;
            for (std::uint32_t x = 0; x < length && !found; ++x) {
                for (std::uint32_t y = 0; y < length && !found; ++y) {
                    auto* cell = grid->GetCell(x, y);
                    if (!cell || !cell->IsAttached()) continue;
                    const auto* coords = cell->GetCoordinates();
                    if (!coords) continue;
                    const float left = static_cast<float>(coords->worldX), bottom = static_cast<float>(coords->worldY);
                    if (left >= centre.x + radius || left + 4096.f <= centre.x - radius || bottom >= centre.y + radius ||
                        bottom + 4096.f <= centre.y - radius) {
                        continue;
                    }
                    cell->ForEachReference([&](RE::TESObjectREFR* ref) {
                        if (!ref || ref->IsDisabled()) return RE::BSContainer::ForEachResult::kContinue;
                        const auto* base = ref->GetBaseObject();
                        if (!base || base->GetFormType() != RE::FormType::Door) return RE::BSContainer::ForEachResult::kContinue;
                        if (!ref->extraList.HasType(RE::ExtraDataType::kTeleport)) return RE::BSContainer::ForEachResult::kContinue;
                        if (Battlefield::Dist({ref->GetPositionX(), ref->GetPositionY()}, centre) > radius) return RE::BSContainer::ForEachResult::kContinue;
                        found = true;
                        return RE::BSContainer::ForEachResult::kStop;
                    });
                }
            }
            return found;
        }
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    bool BattleManager::IsActorAlive(RE::FormID formId) const {
        auto* actor = RE::TESForm::LookupByID<RE::Actor>(formId);
        if (!actor) return false;
        return !actor->IsDead();
    }

    // =========================================================================
    // Battle Lifecycle
    // =========================================================================

    int BattleManager::StartBattle(const std::string& factionA, const std::string& factionB,
                                    const std::string& locationName, int warId) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (activeBattle_.has_value()) {
            logger::warn("[BattleManager] Cannot start battle — one already active (id {})", activeBattle_->id);
            return -1;
        }

        BattleState battle;
        battle.id = nextBattleId_++;
        battle.warId = warId;
        battle.factionA = factionA;
        battle.factionB = factionB;
        battle.locationName = locationName;
        battle.moraleA = 100;
        battle.moraleB = 100;
        battle.currentWave = 0;
        battle.sideAFaction = factionA;      // side A is the battle's first faction, always
        {
            // Settled now, from the battle's own number and the hour: a save plays it one way.
            auto* cal = RE::Calendar::GetSingleton();
            const auto hour = static_cast<std::uint32_t>((cal ? cal->GetCurrentGameTime() : 0.f) * 24.f);
            battle.twists = Battlefield::Plan(static_cast<std::uint32_t>(battle.id) * 2654435761u ^ hour, false);
            logger::info("[BattleManager] Battle {} has in store: champion {}, hidden force {} (after {:.0f} s), turncoats {}", battle.id,
                         battle.twists.champion, battle.twists.flank, battle.twists.flankAfter, battle.twists.turncoat);
        }
        activeBattle_ = std::move(battle);

        logger::info("[BattleManager] Battle {} started: {} vs {} at {}",
                     activeBattle_->id, factionA, factionB, locationName);
        return activeBattle_->id;
    }

    void BattleManager::EndBattle(int battleId, const std::string& result, const std::string& victor) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!activeBattle_.has_value() || activeBattle_->id != battleId) {
            logger::warn("[BattleManager] EndBattle called for id {} but active is {}",
                         battleId, activeBattle_ ? activeBattle_->id : -1);
            return;
        }

        // Preserve actor FormIDs for deferred cleanup — activeBattle_ is about to be reset.
        {
            std::lock_guard<std::mutex> cLock(cleanupMutex_);
            cleanupFormIds_.clear();
            for (const auto& a : activeBattle_->actors) {
                cleanupFormIds_.push_back(a.formId);
            }
            logger::info("[BattleManager] Battle {} ended: result={}, victor={}, {} actors moved to cleanup list",
                         battleId, result, victor, cleanupFormIds_.size());
        }
        // Four minutes for the field to empty, if anyone was on it.
        if (activeBattle_->fieldSet && !activeBattle_->actors.empty()) quietUntil_.store(RealSeconds() + 240.0);
        // And as long for what is done to those who are leaving it to stay no crime.
        if (bountyWatched_) bountyWatchUntil_ = RealSeconds() + 240.0;
        lastPlace_ = activeBattle_->locationName;

        activeBattle_.reset();
    }

    bool BattleManager::IsBattleActive() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeBattle_.has_value();
    }

    bool BattleManager::IsFieldBusy() const {
        if (RealSeconds() < quietUntil_.load()) return true;
        std::lock_guard<std::mutex> lock(mutex_);
        return activeBattle_.has_value() && activeBattle_->fieldSet;
    }

    int BattleManager::GetActiveBattleId() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeBattle_ ? activeBattle_->id : -1;
    }

    // =========================================================================
    // Actor Tracking
    // =========================================================================

    bool BattleManager::RegisterActor(RE::Actor* actor, const std::string& factionId, int tier) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!activeBattle_.has_value()) return false;
        if (!actor) return false;

        BattleActor ba;
        ba.formId = actor->GetFormID();
        ba.factionId = factionId;
        ba.tier = std::clamp(tier, 0, 3);            // 3: a champion
        ba.alive = true;

        activeBattle_->actors.push_back(ba);
        if (activeBattle_->physicalPosition.is_null()) {
            auto* cell = actor->GetParentCell();
            auto* world = actor->GetWorldspace();
            const float x = actor->GetPositionX(), y = actor->GetPositionY(), z = actor->GetPositionZ();
            if (cell && std::isfinite(x) && std::isfinite(y) && std::isfinite(z)) {
                RE::TESForm* space = cell->IsInteriorCell() ? static_cast<RE::TESForm*>(cell) : world;
                auto* file = space ? space->GetFile(0) : nullptr;
                if (file && !space->IsDynamicForm()) {
                    const nlohmann::json identity = {{"plugin", file->fileName}, {"local_id", space->GetLocalFormID()}};
                    activeBattle_->physicalPosition = {
                        {"worldspace", cell->IsInteriorCell() ? nlohmann::json(nullptr) : identity},
                        {"cell", cell->IsInteriorCell() ? identity : nlohmann::json(nullptr)}, {"x", x}, {"y", y}, {"z", z}};
                }
            }
        }
        return true;
    }

    int BattleManager::GetAliveCount(const std::string& factionId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return 0;

        int count = 0;
        for (const auto& a : activeBattle_->actors) {
            if (a.factionId == factionId && a.alive) count++;
        }
        return count;
    }

    // =========================================================================
    // Morale
    // =========================================================================

    int BattleManager::GetMorale(const std::string& factionId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return 0;

        if (factionId == activeBattle_->factionA) return activeBattle_->moraleA;
        if (factionId == activeBattle_->factionB) return activeBattle_->moraleB;
        return 0;
    }

    void BattleManager::AdjustMorale(const std::string& factionId, int delta) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;

        if (factionId == activeBattle_->factionA) {
            activeBattle_->moraleA = std::clamp(activeBattle_->moraleA + delta, 0, Battlefield::kHeartMost);
        } else if (factionId == activeBattle_->factionB) {
            activeBattle_->moraleB = std::clamp(activeBattle_->moraleB + delta, 0, Battlefield::kHeartMost);
        }
    }

    // =========================================================================
    // Wave Management
    // =========================================================================

    int BattleManager::GetCurrentWave() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeBattle_ ? activeBattle_->currentWave : 0;
    }

    int BattleManager::GetWave(const std::string& side) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return 0;
        return IsA(side) ? activeBattle_->waveA : activeBattle_->waveB;
    }

    void BattleManager::AdvanceWaveOf(const std::string& side) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;
        const bool isA = IsA(side);
        int& wave = isA ? activeBattle_->waveA : activeBattle_->waveB;
        ++wave;
        activeBattle_->currentWave = (std::max)(activeBattle_->waveA, activeBattle_->waveB);
        // Those alive now are what this side's next call for reserves is measured against.
        const auto& own = isA ? activeBattle_->factionA : activeBattle_->factionB;
        int alive = 0;
        for (const auto& actor : activeBattle_->actors) {
            if (actor.alive && actor.factionId == own) ++alive;
        }
        (isA ? activeBattle_->waveStartAliveA : activeBattle_->waveStartAliveB) = alive;
        logger::info("[BattleManager] Side {} has its wave {} on the field ({} standing)", side, wave, alive);
    }

    void BattleManager::AdvanceWave() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;
        activeBattle_->currentWave++;
        activeBattle_->waveA = activeBattle_->waveB = activeBattle_->currentWave;

        // Snapshot current alive counts as the casualty baseline for this wave.
        // Without this, cumulative dead from prior waves inflate the casualty rate
        // and trigger the next wave immediately after spawning.
        int aliveA = 0, aliveB = 0;
        for (const auto& actor : activeBattle_->actors) {
            if (!actor.alive) continue;
            if (actor.factionId == activeBattle_->factionA) aliveA++;
            else if (actor.factionId == activeBattle_->factionB) aliveB++;
        }
        activeBattle_->waveStartAliveA = aliveA;
        activeBattle_->waveStartAliveB = aliveB;

        logger::info("[BattleManager] Advanced to wave {} (baseline: A={}, B={})",
                     activeBattle_->currentWave, aliveA, aliveB);
    }

    // =========================================================================
    // Combat Polling
    // =========================================================================

    std::string BattleManager::PollBattleState() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!activeBattle_) return "{}";

        auto& battle = *activeBattle_;

        nlohmann::json result;
        nlohmann::json events = nlohmann::json::array();

        // Single pass: detect deaths, accumulate morale penalties, count alive/total per faction
        int penaltyA = 0, penaltyB = 0;
        int aliveA = 0, aliveB = 0;
        int totalA = 0, totalB = 0;

        for (auto& actor : battle.actors) {
            bool isA = (actor.factionId == battle.factionA);
            if (isA) totalA++; else totalB++;

            if (!actor.alive) continue;

            if (!IsActorAlive(actor.formId)) {
                actor.alive = false;

                int tierPenalty = (actor.tier == 3) ? Battlefield::kLossChampion
                                : (actor.tier == 2) ? (std::max)(MORALE_PENALTY_LEADER, battle.lossPerSoldier) : battle.lossPerSoldier;
                if (actor.tier == 3) battle.championFell = actor.factionId;
                if (isA) penaltyA += tierPenalty;
                else penaltyB += tierPenalty;

                // Track player kills for end-of-battle standing adjustment
                auto* deadActor = RE::TESForm::LookupByID<RE::Actor>(actor.formId);
                if (deadActor) {
                    // Who killed them, from the actor's own record. (Actor::GetKiller of this library answers "nobody"
                    // for anyone who is dead: its first line is `if (IsDead(false)) return nullptr`. Every kill the
                    // player ever made in a battle was counted as nobody's: found 2026-11-20 with six Stormcloaks cut
                    // down by hand and "killsB=0" in the log.)
                    const auto killer = deadActor->GetActorRuntimeData().myKiller.get();
                    if (killer && killer->IsPlayerRef()) {
                        if (isA) battle.playerKillsA++;
                        else battle.playerKillsB++;
                        if (actor.tier == 2) ++(isA ? battle.playerSlewCaptainsA : battle.playerSlewCaptainsB);
                        if (actor.tier == 3) ++(isA ? battle.playerSlewChampionsA : battle.playerSlewChampionsB);
                    }
                }

                // First blood detection
                if (!battle.firstBloodNarrated) {
                    battle.firstBloodNarrated = true;
                    nlohmann::json evt;
                    evt["type"] = "first_blood";
                    evt["faction"] = actor.factionId;
                    evt["tier"] = actor.tier;
                    events.push_back(evt);
                }

                // Leader killed
                if (actor.tier == 2) battle.commanderFell = actor.factionId;
                if (actor.tier == 2) {
                    nlohmann::json evt;
                    evt["type"] = "leader_down";
                    evt["faction"] = actor.factionId;
                    events.push_back(evt);
                }

                // Recruited NPC killed
                if (actor.tier == 1) {
                    nlohmann::json evt;
                    evt["type"] = "recruit_down";
                    evt["faction"] = actor.factionId;
                    evt["form_id"] = actor.formId;
                    events.push_back(evt);
                }
            } else {
                if (isA) aliveA++; else aliveB++;
            }
        }

        // Apply accumulated morale penalties
        if (penaltyA > 0) battle.moraleA = std::clamp(battle.moraleA - penaltyA, 0, Battlefield::kHeartMost);
        if (penaltyB > 0) battle.moraleB = std::clamp(battle.moraleB - penaltyB, 0, Battlefield::kHeartMost);

        // Check morale thresholds (60, 40) — use string compound key to avoid hash collisions
        // Skip threshold 20: it triggers battle_over in the same poll, so a separate event is wasted
        auto checkMoraleThreshold = [&](const std::string& factionId, int morale) {
            for (int threshold : {60, 40}) {
                std::string key = factionId + "_" + std::to_string(threshold);
                if (morale <= threshold && battle.moraleThresholdsNarrated.find(key) == battle.moraleThresholdsNarrated.end()) {
                    battle.moraleThresholdsNarrated.insert(key);
                    nlohmann::json evt;
                    evt["type"] = "morale_threshold";
                    evt["faction"] = factionId;
                    evt["morale"] = morale;
                    evt["threshold"] = threshold;
                    events.push_back(evt);
                }
            }
        };
        checkMoraleThreshold(battle.factionA, battle.moraleA);
        checkMoraleThreshold(battle.factionB, battle.moraleB);

        // Wave spawn conditions — compare alive now vs alive at wave start (not cumulative total).
        // This prevents prior-wave deaths from immediately triggering the next wave.
        auto shouldSpawnWave = [&battle](int waveStartAlive, int alive, int wave) -> bool {
            if (battle.mustering || waveStartAlive == 0 || wave >= battle.waves) return false;
            float casualtyRate = 1.0f - (static_cast<float>(alive) / static_cast<float>(waveStartAlive));
            return casualtyRate >= WAVE_CASUALTY_THRESHOLD;
        };

        // Battle over conditions
        bool battleOver = false;
        std::string battleResult;
        std::string battleVictor;

        // Only count aliveX==0 as defeat after reserves committed (wave 3).
        // Before that, more soldiers haven't spawned yet — don't end prematurely.
        // If the PLAYER is on side A and still alive, side A isn't truly defeated —
        // the player can solo the remaining enemies. Same logic for side B.
        bool playerOnA = (battle.playerSide == battle.factionA);
        bool playerOnB = (battle.playerSide == battle.factionB);
        // (An army with nobody standing is beaten once it has nobody left to send, not before.)
        bool allDeadEndsA = (aliveA == 0 && battle.waveA >= battle.waves && !playerOnA);
        bool allDeadEndsB = (aliveB == 0 && battle.waveB >= battle.waves && !playerOnB);

        // While the lines are only drawn up nothing is decided. After that an army is beaten when its heart is
        // gone or its last man is. Both in the same moment: whoever has the better of it, or nobody (the
        // second-named always won that one).
        const bool brokenA = !battle.mustering && (battle.moraleA <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsA);
        const bool brokenB = !battle.mustering && (battle.moraleB <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsB);
        if (brokenA && brokenB) {
            battleOver = true;
            const int winner = Battlefield::BrokeTogether(aliveA, aliveB, battle.moraleA, battle.moraleB);
            battleVictor = winner == 1 ? battle.factionA : (winner == 2 ? battle.factionB : std::string());
            battleResult = battleVictor.empty() ? std::string("draw") : battleVictor + "_victory";
        } else if (brokenA) {
            battleOver = true;
            battleResult = battle.factionB + "_victory";
            battleVictor = battle.factionB;
        } else if (brokenB) {
            battleOver = true;
            battleResult = battle.factionA + "_victory";
            battleVictor = battle.factionA;
        }

        // If all ENEMY soldiers are dead but player's side still has the player alive,
        // that's a victory for the player's side (player soloed the rest)
        if (!battleOver && !battle.mustering && battle.waveA >= battle.waves && battle.waveB >= battle.waves) {
            if (playerOnA && aliveB == 0 && aliveA == 0) {
                // All soldiers dead on both sides but player survived — player's side wins
                battleOver = true;
                battleResult = battle.factionA + "_victory";
                battleVictor = battle.factionA;
            } else if (playerOnB && aliveA == 0 && aliveB == 0) {
                battleOver = true;
                battleResult = battle.factionB + "_victory";
                battleVictor = battle.factionB;
            }
        }

        // Build result JSON
        result["events"] = events;
        result["morale_a"] = battle.moraleA;
        result["morale_b"] = battle.moraleB;
        result["alive_a"] = aliveA;
        result["alive_b"] = aliveB;
        result["wave"] = battle.currentWave;
        // Serialize booleans as strings — Papyrus StoryResponseGetField extracts string values
        result["should_spawn_wave_a"] = shouldSpawnWave(battle.waveStartAliveA, aliveA, battle.waveA) ? "true" : "false";
        result["should_spawn_wave_b"] = shouldSpawnWave(battle.waveStartAliveB, aliveB, battle.waveB) ? "true" : "false";
        result["battle_over"] = battleOver ? "true" : "false";
        result["mustering"] = battle.mustering ? "true" : "false";
        if (!battle.commanderFell.empty()) {
            result["commander_down"] = battle.commanderFell;         // said once
            battle.commanderFell.clear();
        }
        if (!battle.championFell.empty()) {
            result["champion_down"] = battle.championFell;
            battle.championFell.clear();
        }
        if (!battleOver && !battle.mustering) {
            // A champion comes up when the fighting has gone on a while.
            if (battle.twists.champion != 0 && RealSeconds() - battle.fightBegan >= battle.twists.championAfter) {
                std::string who;
                if ((battle.twists.champion & 1) && !battle.championCalledA) {
                    battle.championCalledA = true;
                    who += "A";
                }
                if ((battle.twists.champion & 2) && !battle.championCalledB) {
                    battle.championCalledB = true;
                    who += "B";
                }
                if (!who.empty()) result["do_champion"] = who;
            }
            // The hidden force breaks cover when its time has come.
            if (battle.twists.flank != 0 && !battle.flankDone && RealSeconds() - battle.fightBegan >= battle.twists.flankAfter) {
                battle.flankDone = true;
                result["do_flank"] = battle.twists.flank == 1 ? "A" : "B";
            }
            // Men turn their coats when their side's heart has sunk.
            if (battle.twists.turncoat != 0 && !battle.turnDone) {
                const bool ofA = battle.twists.turncoat == 1;
                if ((ofA ? battle.moraleA : battle.moraleB) <= Battlefield::kTurnAtMorale && Battlefield::Turncoats(ofA ? aliveA : aliveB) > 0) {
                    battle.turnDone = true;
                    result["do_turn"] = ofA ? "A" : "B";
                }
            }
        }
        if (battleOver) {
            // How it stood when it was decided: the one line that says why a battle ended as it did.
            logger::info("[BattleManager] Battle {} is decided ({}): {} heart {}, {} standing, wave {}; {} heart {}, {} standing, wave {}", battle.id,
                         battleResult, battle.factionA, battle.moraleA, aliveA, battle.waveA, battle.factionB, battle.moraleB, aliveB, battle.waveB);
            result["result"] = battleResult;
            result["victor"] = battleVictor;
            result["player_kills_a"] = battle.playerKillsA;
            result["player_kills_b"] = battle.playerKillsB;
        }

        // Include player side in output
        result["player_side"] = battle.playerSide;
        result["player_participated"] = battle.playerParticipated ? "true" : "false";

        // Continuous bounty suppression — clear bounty and stop hostile guards every poll.
        // Crime faction removal doesn't prevent bounty (engine uses cell crime faction, not membership).
        if (!battle.playerSide.empty()) {
            suppressBattleBounty_ = true;
        }

        return result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    // What happens in a battle is not the hold's business. A blow in the press that kills a man of the player's own
    // side is murder to the game, and the bounty goes to the hold it happens in, whatever factions the dead man was
    // in (played 2026-11-21 with a heavy hand: 1,000 gold in Whiterun, and three guards of the road on a Captain of
    // the Legion in the middle of his battle). The ladder has its own price for such a blow (Ranks: StruckOwn), and
    // word of it goes round. So what the player owes for violent crime is noted when they take a side, and what has
    // come on top while the battle is fought, and for four minutes after while its field empties, is struck out
    // again; the guards who came for it stand down. What was owed before stays owed, in every hold; theft and
    // trespass are not looked at. (3.8 set "every hold's" bounty to nothing on every poll, with a list of ids of
    // which one was a hold's: whoever owed Solitude gold lost the debt by joining a battle anywhere.)
    void BattleManager::SnapshotBounty() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        // Every faction that keeps a bounty, asked through the game's own calls. (The player's own table of crime gold
        // was read first: on this version of the game it came back empty, and a bounty of 700 stayed. 2026-11-21.)
        if (crimeFactions_.empty()) {
            if (auto* data = RE::TESDataHandler::GetSingleton()) {
                for (auto* faction : data->GetFormArray<RE::TESFaction>()) {
                    if (faction && (static_cast<std::uint32_t>(faction->data.flags) & RE::FACTION_DATA::Flag::kTrackCrime) != 0) crimeFactions_.push_back(faction);
                }
            }
        }
        bountyBefore_.clear();
        int owing = 0;
        for (auto* faction : crimeFactions_) {
            const auto owed = static_cast<float>(faction->GetCrimeGoldViolent());
            bountyBefore_[faction->GetFormID()] = owed;
            if (owed > 0.f) ++owing;
        }
        bountyWatched_ = true;
        bountyWatchUntil_ = 0.0;
        logger::info("Battle: what the player owes for violent crime is noted ({} factions keep a bounty, {} are owed one)", crimeFactions_.size(), owing);
    }

    void BattleManager::StrikeBattleBounty() {
        if (!bountyWatched_) return;
        if (bountyWatchUntil_ > 0.0 && RealSeconds() > bountyWatchUntil_) {
            bountyWatched_ = false;
            return;
        }
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        // Collected first: striking it out changes the table that is being read.
        std::vector<std::pair<RE::TESFaction*, float>> over;
        for (auto* faction : crimeFactions_) {
            const auto before = bountyBefore_.find(faction->GetFormID());
            const float owed = before == bountyBefore_.end() ? 0.f : before->second;
            const auto now = static_cast<float>(faction->GetCrimeGoldViolent());
            if (now > owed + 0.5f) over.emplace_back(faction, now - owed);
        }
        if (over.empty()) return;
        for (const auto& [faction, extra] : over) {
            faction->ModCrimeGold(-static_cast<std::int32_t>(std::lround(extra)), true);
            logger::info("Battle: {:.0f} gold of bounty with {} for what happened in the battle is struck out", extra,
                         faction->GetFullName() && *faction->GetFullName() ? faction->GetFullName() : "a hold");
        }
        // The guards who came for it stand down (nobody of the battle: its soldiers have their own reasons).
        player->StopAlarmOnActor();
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            auto [sideA, sideB] = ResolveBattleFactions();
            for (const auto& handle : lists->highActorHandles) {
                const auto held = handle.get();
                auto* actor = held.get();
                if (!actor || actor->IsDead() || !actor->Is3DLoaded() || !actor->IsGuard()) continue;
                if ((sideA && actor->IsInFaction(sideA)) || (sideB && actor->IsInFaction(sideB))) continue;
                if (!actor->IsHostileToActor(player)) continue;
                actor->SetBeenAttacked(false);
                actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kAngryWithPlayer);
                actor->StopCombat();
                logger::info("Battle: {} ({:08X}) stands down: a battle is no crime", actor->GetDisplayFullName(), actor->GetFormID());
            }
        }
    }

    void BattleManager::SuppressBountyTick() {
        StrikeBattleBounty();
        if (!suppressBattleBounty_) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        // A stray blow in the press: the soldiers of the player's own side forget it. Only they: this used to go
        // through every loaded person of the faction, guards and townsfolk too, and wipe their memory of an assault.
        std::vector<RE::FormID> own;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || activeBattle_->playerSide.empty()) return;
            for (const auto& a : activeBattle_->actors) {
                if (a.alive && a.factionId == activeBattle_->playerSide) own.push_back(a.formId);
            }
        }
        for (const auto id : own) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
            if (!actor || actor->IsDead() || !actor->Is3DLoaded() || !actor->IsHostileToActor(player)) continue;
            actor->SetBeenAttacked(false);
            actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kAngryWithPlayer);
            actor->StopCombat();
            logger::info("Battle: {} ({:08X}) of the player's own side forgets a stray blow", actor->GetDisplayFullName(), actor->GetFormID());
        }
    }

    // =========================================================================
    // Player Participation
    // =========================================================================

    bool BattleManager::SetPlayerSide(const std::string& factionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return false;

        // Validate faction is part of this battle (or empty to leave)
        if (!factionId.empty() &&
            factionId != activeBattle_->factionA &&
            factionId != activeBattle_->factionB) {
            logger::warn("[BattleManager] SetPlayerSide: {} is not in this battle", factionId);
            return false;
        }

        // If already on this side, no-op
        if (activeBattle_->playerSide == factionId) return true;

        std::string oldSide = activeBattle_->playerSide;
        activeBattle_->playerSide = factionId;

        // What the player's coming is worth to the side they join, and costs the other. (Capped at 100 it was worth
        // nothing to a side joined before the fight; and whoever changed sides left the first one its gain.)
        const auto shift = [this](const std::string& side, int sign) {
            int& own = side == activeBattle_->factionA ? activeBattle_->moraleA : activeBattle_->moraleB;
            int& other = side == activeBattle_->factionA ? activeBattle_->moraleB : activeBattle_->moraleA;
            own = std::clamp(own + sign * MORALE_PLAYER_JOIN_BOOST, 0, Battlefield::kHeartMost);
            other = std::clamp(other - sign * MORALE_PLAYER_JOIN_ENEMY_PENALTY, 0, Battlefield::kHeartMost);
        };
        if (!oldSide.empty()) shift(oldSide, -1);

        if (!factionId.empty()) {
            activeBattle_->playerParticipated = true;
            shift(factionId, +1);
            if (!bountyWatched_ || bountyWatchUntil_ > 0.0) SnapshotBounty();

            logger::info("[BattleManager] Player joined side: {} (hearts now {} {} / {} {})", factionId, activeBattle_->factionA, activeBattle_->moraleA,
                         activeBattle_->factionB, activeBattle_->moraleB);
        } else {
            logger::info("[BattleManager] Player left battle (was: {})", oldSide);
        }

        return true;
    }

    std::string BattleManager::GetPlayerSide() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return "";
        return activeBattle_->playerSide;
    }

    std::string BattleManager::GetFactionSide(const std::string& factionId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return "A";
        return (factionId == activeBattle_->factionA) ? "A" : "B";
    }

    bool BattleManager::HasPlayerParticipated() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return false;
        return activeBattle_->playerParticipated;
    }

    bool BattleManager::IsBattleFaction(const std::string& factionId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return false;
        return factionId == activeBattle_->factionA || factionId == activeBattle_->factionB;
    }

    BattleManager::BattleSnapshot BattleManager::GetBattleSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        BattleSnapshot snap;
        if (activeBattle_) {
            snap.active = true;
            snap.playerParticipated = activeBattle_->playerParticipated;
            snap.factionA = activeBattle_->factionA;
            snap.factionB = activeBattle_->factionB;
        }
        return snap;
    }

    // =========================================================================
    // Pending Battles
    // =========================================================================

    int BattleManager::AddPendingBattle(const std::string& locationName,
                                         const std::string& factionA, const std::string& factionB,
                                         const std::string& resultJson) {
        // Resolve location name to world coordinates via LocationResolver
        auto* resolver = LocationResolver::GetSingleton();
        std::string resolvedName = locationName;
        auto* bgsLocation = resolver->ResolveLocation(resolvedName);

        // Fallback: LLM sometimes appends hold name (e.g., "Kynesgrove, Eastmarch")
        // Strip the suffix after comma and retry
        if (!bgsLocation) {
            auto commaPos = locationName.find(',');
            if (commaPos != std::string::npos) {
                std::string stripped = locationName.substr(0, commaPos);
                while (!stripped.empty() && stripped.back() == ' ') stripped.pop_back();
                if (!stripped.empty()) {
                    logger::info("[BattleManager] AddPendingBattle: Retrying with stripped name '{}'", stripped);
                    bgsLocation = resolver->ResolveLocation(stripped);
                    if (bgsLocation) {
                        resolvedName = stripped;
                    }
                }
            }
        }

        // Fallback: use involved faction's hold city (always resolvable)
        if (!bgsLocation) {
            auto* politics = FactionPolitics::GetSingleton();
            // Try factionB's hold first (defender's territory), then factionA's
            for (const auto& fid : {factionB, factionA}) {
                auto cfg = politics->GetFaction(fid);
                if (cfg && !cfg->hold.empty()) {
                    bgsLocation = resolver->ResolveLocation(cfg->hold);
                    if (bgsLocation) {
                        resolvedName = cfg->hold;
                        logger::info("[BattleManager] AddPendingBattle: Using faction hold '{}' as fallback for '{}'",
                                     cfg->hold, locationName);
                        break;
                    }
                }
            }
        }

        if (!bgsLocation) {
            logger::warn("[BattleManager] AddPendingBattle: Could not resolve location '{}' (all fallbacks failed)", locationName);
            return -1;
        }

        auto* markerRef = std::as_const(bgsLocation->worldLocMarker).get().get();
        if (!markerRef) {
            // Fallback: FindTravelTarget tries cell doors, exterior cells, etc.
            markerRef = resolver->FindTravelTarget(resolvedName);
            if (!markerRef) {
                logger::warn("[BattleManager] AddPendingBattle: No worldLocMarker or travel target for '{}'", locationName);
                return -1;
            }
            logger::info("[BattleManager] AddPendingBattle: Using FindTravelTarget fallback for '{}'", locationName);
        }

        auto pos = markerRef->GetPosition();

        // Get current game time for deadline
        auto* cal = RE::Calendar::GetSingleton();
        float now = cal ? cal->GetCurrentGameTime() : 0.0f;

        std::lock_guard<std::mutex> lock(pendingMutex_);

        PendingBattle pb;
        pb.id = nextPendingId_++;
        pb.factionA = factionA;
        pb.factionB = factionB;
        pb.locationName = resolvedName;
        pb.x = pos.x;
        pb.y = pos.y;
        pb.z = pos.z;
        pb.deadline = now + PENDING_BATTLE_DURATION;
        pb.resultJson = resultJson;
        if (auto* world = markerRef->GetWorldspace()) pb.worldspace = world->GetFormID();
        pb.hold = HoldNameOf(bgsLocation);

        pendingBattles_.push_back(std::move(pb));

        logger::info("[BattleManager] Pending battle {} added: {} vs {} at {} ({:.0f}, {:.0f}, {:.0f}), deadline {:.4f}",
                     pendingBattles_.back().id, factionA, factionB, locationName, pos.x, pos.y, pos.z, pendingBattles_.back().deadline);

        return pendingBattles_.back().id;
    }

    int BattleManager::PollPendingBattles(float playerX, float playerY, RE::FormID worldspace, bool indoors) {
        auto* cal = RE::Calendar::GetSingleton();
        float now = cal ? cal->GetCurrentGameTime() : 0.0f;

        // Collect expired battles under lock, then process outside lock to avoid
        // holding pendingMutex_ while calling RecordOffScreenBattle (which acquires DB mutex).
        // This prevents lock-order inversion deadlocks.
        struct ExpiredBattle {
            std::string factionA, factionB, locationName, resultJson, hold;
        };
        std::vector<ExpiredBattle> expired;
        int triggeredId = -1;

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);

            // First pass: collect expired battles
            for (auto it = pendingBattles_.begin(); it != pendingBattles_.end(); ) {
                if (now >= it->deadline) {
                    logger::info("[BattleManager] Pending battle {} at {} was not seen: {} vs {} is decided unseen",
                                 it->id, it->locationName, it->factionA, it->factionB);
                    expired.push_back({it->factionA, it->factionB, it->locationName, it->resultJson, it->hold});
                    it = pendingBattles_.erase(it);
                } else {
                    ++it;
                }
            }

            // Second pass: the player near one of them, in the same world and out of doors. (3.8 measured on the
            // map alone: a battle "at Whiterun" began for a player inside Dragonsreach, and one at a place of the
            // same coordinates in another world, Sovngarde or Solstheim, for a player there.)
            if (!indoors && worldspace != 0) {
                for (auto& pb : pendingBattles_) {
                    if (pb.worldspace != worldspace) continue;
                    float dx = playerX - pb.x;
                    float dy = playerY - pb.y;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist <= Battlefield::kTriggerDistance) {
                        triggeredId = pb.id;
                        break;
                    }
                }
            }
        }  // pendingMutex_ released

        // Decide the battles nobody saw, outside the lock, and tell of them.
        auto* fp = FactionPolitics::GetSingleton();
        for (const auto& eb : expired) {
            std::string battleResult, victor, narrative;
            int lossesA = 0, lossesB = 0;
            try {
                auto j = nlohmann::json::parse(eb.resultJson);
                if (j.value("event_type", std::string()) == "battle_result") {
                    battleResult = j.value("battle_result", std::string());
                    victor = j.value("battle_victor", std::string());
                    lossesA = std::clamp(j.value("attacker_losses", 0), 0, 30);
                    lossesB = std::clamp(j.value("defender_losses", 0), 0, 30);
                    narrative = j.value("description", std::string());
                    if (victor.empty() && battleResult == "attacker_victory") victor = eb.factionA;
                    if (victor.empty() && battleResult == "defender_victory") victor = eb.factionB;
                    if (victor != eb.factionA && victor != eb.factionB) victor.clear();
                }
            } catch (const std::exception& e) {
                logger::warn("[BattleManager] the outcome written for the battle at {} cannot be read ({}): it is decided here", eb.locationName, e.what());
                battleResult.clear();
            }
            if (battleResult.empty() || narrative.empty()) {
                // No outcome was written for it (a battle that was only announced): the side that stands better in
                // the war has the better chance.
                const auto war = PoliticalDB::GetSingleton()->GetActiveWar(eb.factionA, eb.factionB);
                if (!war) {
                    logger::info("[BattleManager] {} and {} are no longer at war: the battle at {} is not fought", eb.factionA, eb.factionB, eb.locationName);
                    continue;
                }
                const bool aIsWarA = eb.factionA == war->factionA;
                const float scoreA = static_cast<float>(aIsWarA ? war->factionAMorale + war->factionAStrength / 2 : war->factionBMorale + war->factionBStrength / 2) + RandomFloat(0.f, 45.f);
                const float scoreB = static_cast<float>(aIsWarA ? war->factionBMorale + war->factionBStrength / 2 : war->factionAMorale + war->factionAStrength / 2) + RandomFloat(0.f, 45.f);
                victor = scoreA >= scoreB ? eb.factionA : eb.factionB;
                battleResult = victor == eb.factionA ? "attacker_victory" : "defender_victory";
                lossesA = static_cast<int>(RandomFloat(victor == eb.factionA ? 4.f : 8.f, victor == eb.factionA ? 10.f : 16.f));
                lossesB = static_cast<int>(RandomFloat(victor == eb.factionB ? 4.f : 8.f, victor == eb.factionB ? 10.f : 16.f));
                const std::string loser = victor == eb.factionA ? eb.factionB : eb.factionA;
                narrative = CapThe(fp->GetFactionDisplayName(victor)) + " defeated " + The(fp->GetFactionDisplayName(loser)) + " near " + eb.locationName + ".";
            }

            if (fp->RecordOffScreenBattle(eb.factionA, eb.factionB, eb.locationName, battleResult, narrative, lossesA, lossesB, victor) < 0) continue;
            // Now it has happened, and now it is told: where it was fought first.
            fp->RecordPoliticalEvent(eb.factionA, eb.factionB, "battle_result", narrative, 0, now, eb.locationName, nullptr, eb.hold);

            // For the script: word of it reaches a player who is in that hold.
            nlohmann::json expiredInfo;
            expiredInfo["faction_a"] = eb.factionA;
            expiredInfo["faction_b"] = eb.factionB;
            expiredInfo["location"] = eb.locationName;
            expiredInfo["victor"] = victor;
            expiredInfo["result"] = battleResult;
            expiredInfo["heard"] = PlayerIsInHold(eb.hold) ? "true" : "false";
            {
                std::lock_guard<std::mutex> lock(pendingMutex_);
                expiredResults_.push_back(expiredInfo.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
            }
        }

        return triggeredId;
    }

    bool BattleManager::HasPendingBattle(const std::string& factionA, const std::string& factionB) const {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (const auto& pb : pendingBattles_) {
            if ((pb.factionA == factionA && pb.factionB == factionB) || (pb.factionA == factionB && pb.factionB == factionA)) return true;
        }
        return false;
    }

    bool BattleManager::PlayerIsInHold(const std::string& hold) const {
        auto* player = RE::PlayerCharacter::GetSingleton();
        return player && !hold.empty() && NPCIndex::GetNPCHoldName(player) == hold;
    }

    std::pair<std::string, std::string> BattleManager::PendingBattlePlace(int id) const {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (const auto& pb : pendingBattles_) {
            if (pb.id == id) return {pb.locationName, pb.hold};
        }
        return {};
    }

    void BattleManager::ClearPendingBattles() {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingBattles_.clear();
        expiredResults_.clear();
        logger::info("[BattleManager] Cleared all pending battles and expired results");
    }

    void BattleManager::Save(SKSE::SerializationInterface* intfc) const {
        std::vector<PendingBattle> pending;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pending = pendingBattles_;
        }
        if (!intfc->OpenRecord(kPendingBattleRecord, 1)) {
            logger::error("[BattleManager] could not open the co-save record of the gathered armies");
            return;
        }
        const auto put32 = [intfc](std::uint32_t v) { intfc->WriteRecordData(&v, sizeof(v)); };
        const auto putf = [intfc](float v) { intfc->WriteRecordData(&v, sizeof(v)); };
        const auto puts = [intfc, &put32](const std::string& s) {
            put32(static_cast<std::uint32_t>(s.size()));
            if (!s.empty()) intfc->WriteRecordData(s.data(), static_cast<std::uint32_t>(s.size()));
        };
        put32(static_cast<std::uint32_t>(pending.size()));
        for (const auto& pb : pending) {
            put32(static_cast<std::uint32_t>(pb.id));
            putf(pb.deadline);
            putf(pb.x);
            putf(pb.y);
            putf(pb.z);
            put32(pb.worldspace);
            puts(pb.factionA);
            puts(pb.factionB);
            puts(pb.locationName);
            puts(pb.hold);
            puts(pb.resultJson);
        }
    }

    void BattleManager::Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version != 1 || length > (1u << 20)) {
            logger::warn("[BattleManager] skipping a co-save record of gathered armies of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("[BattleManager] the co-save record of gathered armies is short: nobody is gathered anywhere");
            return;
        }
        std::size_t at = 0;
        const auto get32 = [&bytes, &at](std::uint32_t& v) {
            if (at + sizeof(v) > bytes.size()) return false;
            std::memcpy(&v, bytes.data() + at, sizeof(v));
            at += sizeof(v);
            return true;
        };
        const auto getf = [&bytes, &at](float& v) {
            if (at + sizeof(v) > bytes.size()) return false;
            std::memcpy(&v, bytes.data() + at, sizeof(v));
            at += sizeof(v);
            return true;
        };
        const auto gets = [&bytes, &at, &get32](std::string& s) {
            std::uint32_t size = 0;
            if (!get32(size) || size > bytes.size() - at) return false;
            s.assign(reinterpret_cast<const char*>(bytes.data() + at), size);
            at += size;
            return true;
        };
        std::uint32_t count = 0;
        if (!get32(count) || count > 64) return;
        std::vector<PendingBattle> loaded;
        int highest = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            PendingBattle pb;
            std::uint32_t id = 0, world = 0;
            if (!get32(id) || !getf(pb.deadline) || !getf(pb.x) || !getf(pb.y) || !getf(pb.z) || !get32(world) || !gets(pb.factionA) ||
                !gets(pb.factionB) || !gets(pb.locationName) || !gets(pb.hold) || !gets(pb.resultJson)) {
                logger::warn("[BattleManager] the co-save record of gathered armies ends early: {} of {} read", loaded.size(), count);
                break;
            }
            pb.id = static_cast<int>(id);
            // The world it lies in, by its id in this load order (a mod's world may have moved; one that is gone
            // leaves a battle nobody can walk to, decided unseen when its time is up).
            RE::FormID resolved = 0;
            pb.worldspace = (world != 0 && intfc->ResolveFormID(world, resolved)) ? resolved : 0;
            highest = (std::max)(highest, pb.id);
            loaded.push_back(std::move(pb));
        }
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingBattles_ = std::move(loaded);
        expiredResults_.clear();
        nextPendingId_ = (std::max)(nextPendingId_, highest + 1);
        logger::info("[BattleManager] {} gathering(s) of armies come back with the save", pendingBattles_.size());
    }

    void BattleManager::Revert() {
        ClearPendingBattles();
        quietUntil_.store(0.0);
        std::lock_guard<std::mutex> lock(templatesMutex_);
        templates_.clear();
    }

    void BattleManager::RemovePendingBattle(int id) {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingBattles_.erase(
            std::remove_if(pendingBattles_.begin(), pendingBattles_.end(),
                           [id](const PendingBattle& pb) { return pb.id == id; }),
            pendingBattles_.end());
    }

    std::string BattleManager::GetPendingBattleInfo(int id) const {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (const auto& pb : pendingBattles_) {
            if (pb.id == id) {
                nlohmann::json j;
                j["id"] = pb.id;
                j["faction_a"] = pb.factionA;
                j["faction_b"] = pb.factionB;
                j["location"] = pb.locationName;
                j["x"] = pb.x;
                j["y"] = pb.y;
                j["z"] = pb.z;
                j["deadline"] = pb.deadline;
                j["hold"] = pb.hold;
                j["result_json"] = pb.resultJson;
                return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
        }
        return "{}";
    }

    int BattleManager::GetPendingBattleCount() const {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        return static_cast<int>(pendingBattles_.size());
    }

    std::string BattleManager::GetLastExpiredBattleResult() {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        if (expiredResults_.empty()) return "";
        std::string result = std::move(expiredResults_.front());
        expiredResults_.erase(expiredResults_.begin());
        return result;
    }

    // =========================================================================
    // Phase 1 Migration: Logic moved from Papyrus to C++
    // =========================================================================

    std::string BattleManager::FinalizeBattle(int battleId, const std::string& result,
                                               const std::string& victor, int deadA, int deadB,
                                               const std::string& locationName, float gameTime) {
        nlohmann::json out;
        std::string playerSideWas;
        std::string factionA, factionB;
        int playerKillsA = 0, playerKillsB = 0;
        bool playerLeft = false;
        bool skirmish = false;
        int slewCaptainsA = 0, slewCaptainsB = 0, slewChampionsA = 0, slewChampionsB = 0;
        std::unordered_set<std::string> turnedDown;
        nlohmann::json physicalPosition = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || activeBattle_->id != battleId) {
                out["error"] = "no matching active battle";
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            skirmish = activeBattle_->skirmish;
            slewCaptainsA = activeBattle_->playerSlewCaptainsA;
            slewCaptainsB = activeBattle_->playerSlewCaptainsB;
            slewChampionsA = activeBattle_->playerSlewChampionsA;
            slewChampionsB = activeBattle_->playerSlewChampionsB;
            playerSideWas = activeBattle_->playerSide;
            factionA = activeBattle_->factionA;
            factionB = activeBattle_->factionB;
            playerKillsA = activeBattle_->playerKillsA;
            playerKillsB = activeBattle_->playerKillsB;
            playerLeft = activeBattle_->playerLeft;
            turnedDown = activeBattle_->turnedDown;
            physicalPosition = activeBattle_->physicalPosition;
        }

        auto* fp = FactionPolitics::GetSingleton();
        // Nobody won (it went on too long, both broke at once, the player left an even field). 3.8 had no such
        // outcome: a battle cut short was not recorded at all, and of two armies that broke in the same moment the
        // second-named was always the winner.
        const bool drawn = victor != factionA && victor != factionB;
        const std::string nameA = fp->GetFactionDisplayName(factionA);
        const std::string nameB = fp->GetFactionDisplayName(factionB);
        const std::string victorName = drawn ? std::string() : fp->GetFactionDisplayName(victor);
        const std::string loser = drawn ? std::string() : (victor == factionA ? factionB : factionA);
        const std::string loserName = drawn ? std::string() : fp->GetFactionDisplayName(loser);

        // --- What the player did ---
        const bool fought = !playerSideWas.empty();
        const std::string enemyFaction = fought ? (playerSideWas == factionA ? factionB : factionA) : std::string();
        const int friendlyKills = fought ? (playerSideWas == factionA ? playerKillsA : playerKillsB) : 0;
        const int enemyKills = fought ? (playerSideWas == factionA ? playerKillsB : playerKillsA) : 0;
        const bool playerWon = fought && !drawn && !playerLeft && playerSideWas == victor;
        // Captains and champions of the other side cut down by the player's own hand.
        const int enemyCaptains = fought ? (playerSideWas == factionA ? slewCaptainsB : slewCaptainsA) : 0;
        const int enemyChampions = fought ? (playerSideWas == factionA ? slewChampionsB : slewChampionsA) : 0;
        // The ladder (Ranks.h): what was done in this battle is what the faction will know of the player.
        const int rankBefore = fought ? fp->PlayerRank(playerSideWas) : 0;
        std::string promoted;
        if (fought) {
            int risen = 0;
            const auto earn = [&](Ranks::Deed deed, int times = 1) { risen = (std::max)(risen, fp->EarnRenown(playerSideWas, deed, times)); };
            if (playerLeft) {
                earn(Ranks::Deed::Deserted);
            } else {
                if (!skirmish) {
                    earn(Ranks::Deed::Fought);
                    if (playerWon) earn(Ranks::Deed::Won);
                }
                earn(Ranks::Deed::Kill, enemyKills);
                earn(Ranks::Deed::Captain, enemyCaptains);
                earn(Ranks::Deed::Champion, enemyChampions);
            }
            earn(Ranks::Deed::StruckOwn, friendlyKills);
            if (risen > rankBefore && fp->PlayerRank(playerSideWas) >= risen) promoted = fp->RankTitle(playerSideWas, risen);
        }
        const int rankNow = fought ? fp->PlayerRank(playerSideWas) : 0;
        const int pay = Ranks::Pay(Battlefield::Pay(playerWon, enemyKills) + (playerWon ? enemyChampions * Battlefield::kChampionPay : 0), rankNow);
        bool spectatorPenalized = false;
        std::string slighted;       // a side that called the player to its line in vain, and minds

        if (friendlyKills > 0) {
            int penalty = friendlyKills * KILL_STANDING_PENALTY_PER_SOLDIER;
            fp->AdjustPlayerStanding(playerSideWas, penalty);
            logger::info("Battle: the player killed {} soldiers of their own side, {} ({} standing)", friendlyKills, playerSideWas, penalty);
        }
        if (fought) {
            if (playerLeft) {
                // Took a place in the line and left it: their own side remembers that; the other has nothing to thank them for.
                fp->AdjustPlayerStanding(playerSideWas, DESERTION_PENALTY);
                fp->AdjustPlayerStanding(enemyFaction, DEFEAT_ENEMY_PENALTY);
            } else if (playerWon) {
                fp->AdjustPlayerStanding(playerSideWas, VICTORY_ALLY_BONUS);
                fp->AdjustPlayerStanding(enemyFaction, VICTORY_ENEMY_PENALTY);
            } else {
                // Lost, or nobody won: they stood with them all the same.
                fp->AdjustPlayerStanding(playerSideWas, DEFEAT_ALLY_BONUS);
                fp->AdjustPlayerStanding(enemyFaction, DEFEAT_ENEMY_PENALTY);
            }
        } else {
            // Took no side. Whoever they cut down remembers it.
            if (playerKillsA > 0) fp->AdjustPlayerStanding(factionA, (std::max)(NEUTRAL_KILL_PENALTY_MOST, playerKillsA * NEUTRAL_KILL_PENALTY));
            if (playerKillsB > 0) fp->AdjustPlayerStanding(factionB, (std::max)(NEUTRAL_KILL_PENALTY_MOST, playerKillsB * NEUTRAL_KILL_PENALTY));
            // And a side that counts them a friend, called them to its line and was turned down. Only that: 3.8
            // docked the standing of anyone who was merely near a battle of a faction that liked them, asked or not.
            // A friend of both sides may stand aside.
            const int standingA = fp->GetPlayerStanding(factionA);
            const int standingB = fp->GetPlayerStanding(factionB);
            if (!(standingA >= AUTO_JOIN_STANDING_THRESHOLD && standingB >= AUTO_JOIN_STANDING_THRESHOLD)) {
                for (const auto& id : {factionA, factionB}) {
                    if (!turnedDown.count(id) || fp->GetPlayerStanding(id) < AUTO_JOIN_STANDING_THRESHOLD) continue;
                    fp->AdjustPlayerStanding(id, SPECTATOR_PENALTY);
                    spectatorPenalized = true;
                    slighted = id;
                }
            }
        }

        // Write updated standings
        fp->WritePoliticalStateFile();

        // --- What is told of it ---
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::string playerName = player ? player->GetDisplayFullName() : "the Dragonborn";
        std::string playerDesc = "someone matching the description of " + playerName;
        // Word of it starts where it was fought. (It started where the first faction sits: a battle at Whiterun was
        // first talked of in Solitude.)
        // (Out on the field the player may stand in no named place at all, and it was told from the first faction's
        // seat after all: then the place's name is given, which is no hold's, and the talk starts in the hold the
        // player was last in.)
        std::string foughtIn = player ? NPCIndex::GetNPCHoldName(player) : std::string();
        if (foughtIn.empty()) foughtIn = locationName.empty() ? std::string("here") : locationName;
        const std::string playerDescCap = "S" + playerDesc.substr(1);

        std::string narrative = drawn
            ? CapThe(nameA) + " and " + The(nameB) + " fought each other to a standstill at " + locationName + ", and both drew off"
            : CapThe(victorName) + " defeated " + The(loserName) + " at " + locationName;
        if (skirmish) {
            narrative = drawn ? "Men of " + The(nameA) + " and of " + The(nameB) + " came to blows near " + locationName + " and parted, neither the better for it"
                              : "Men of " + The(victorName) + " and of " + The(loserName) + " came to blows near " + locationName + ", and those of " +
                                    The(victorName) + " had the better of it";
        }
        if (fought) {
            narrative += ". S" + playerDesc.substr(1) + " was seen fighting for " + The(fp->GetFactionDisplayName(playerSideWas));
            if (enemyKills > 0) narrative += " and reportedly killed " + std::to_string(enemyKills) + " of the other side";
            if (enemyChampions > 0) narrative += ", their champion among them";
            else if (enemyCaptains > 0) narrative += ", their captain among them";
            if (playerLeft) narrative += ", but left the field before it was decided";
        } else if (playerKillsA + playerKillsB > 0) {
            narrative += ". S" + playerDesc.substr(1) + ", of neither side, was seen cutting down soldiers there";
        }

        // --- Record in political DB ---
        // Check for active war first. If war exists, record via RecordOffScreenBattle (updates war state).
        // If no war, record a single political event. Never both — prevents duplicate notifications.
        int warId = fp->GetActiveWarId(factionA, factionB);
        if (warId > 0 && !skirmish) {
            fp->RecordOffScreenBattle(factionA, factionB, locationName, drawn ? "draw" : result, narrative, deadA, deadB, drawn ? std::string() : victor,
                                      &physicalPosition, foughtIn);
        } else {
            // Single combined event with both factions — adjusts inter-faction relation via delta
            int relationDelta = skirmish ? -4 : -10;  // blood between them worsens things, a battle more than a scuffle
            fp->RecordPoliticalEvent(factionA, factionB, skirmish ? "border_skirmish" : "battle_result", narrative, relationDelta, gameTime, locationName,
                                     physicalPosition, foughtIn);
            logger::info("Battle: No active war — recorded battle_result event (relation {} between {} and {})",
                relationDelta, factionA, factionB);
        }

        // Record player participation standing as a separate single-faction event
        if (fought) {
            int allyChange = playerLeft ? DESERTION_PENALTY : (playerWon ? VICTORY_ALLY_BONUS : DEFEAT_ALLY_BONUS);
            std::string allyName = fp->GetFactionDisplayName(playerSideWas);
            std::string participationDesc = playerDescCap + (playerLeft ? " took a place in the line of " : " fought for ") + The(allyName) +
                " at " + locationName + (playerLeft ? " and left it before the end" : (playerWon ? " (victory)" : (drawn ? " (no victor)" : " (defeat)")));
            fp->RecordPoliticalEvent(playerSideWas, "", "player_battle", participationDesc, allyChange, gameTime, locationName, physicalPosition);
        }

        // Record friendly fire as a SEPARATE event (only if it happened)
        if (friendlyKills > 0) {
            int penalty = friendlyKills * KILL_STANDING_PENALTY_PER_SOLDIER;
            // ("someone ... reportedly attacked 1 allied Imperial Legion soldiers": they were killed, and one is one.)
            std::string killDesc = playerDescCap + " cut down " +
                (friendlyKills == 1 ? std::string("a soldier") : std::to_string(friendlyKills) + " soldiers") + " of their own side, " +
                The(fp->GetFactionDisplayName(playerSideWas)) + ", during the battle at " + locationName;
            fp->RecordPoliticalEvent(playerSideWas, "", "friendly_fire", killDesc, penalty, gameTime, locationName, physicalPosition, foughtIn);
        }

        // --- What those who saw it remember ---
        std::string fact = std::string(skirmish ? "saw men of " : "witnessed a battle between ") + The(nameA) + (skirmish ? " and of " : " and ") + The(nameB) +
            (skirmish ? " come to blows near " : " at ") + locationName + ". " +
            (drawn ? std::string("Neither side won it") : CapThe(victorName) + " won it");
        int totalCasualties = deadA + deadB;
        if (totalCasualties > 0) {
            fact += ", and " + std::to_string(totalCasualties) + " lay dead";
        }
        if (fought) {
            fact += ". Someone resembling " + playerName + " fought for " + The(fp->GetFactionDisplayName(playerSideWas));
            if (enemyKills > 0) fact += " and killed " + std::to_string(enemyKills) + " of the other side";
            if (playerLeft) fact += ", then left the field";
        }
        // Store fact for Papyrus to inject (C++ can't call SkyrimNet InjectFact directly)
        out["witnessFact"] = fact;

        // --- End battle in C++ ---
        EndBattle(battleId, result, victor);

        // --- Build return JSON ---
        out["playerSideWas"] = playerSideWas;
        out["playerLeft"] = playerLeft;
        out["playerWon"] = playerWon;
        out["drawn"] = drawn;
        out["playerLeft"] = playerLeft;
        out["victorName"] = victorName;
        out["loserName"] = loserName;
        out["spectatorPenalized"] = spectatorPenalized;
        out["factionA"] = factionA;
        out["factionB"] = factionB;
        out["playerKillsA"] = playerKillsA;
        out["playerKillsB"] = playerKillsB;
        out["enemyKills"] = enemyKills;
        // What the winning side pays one who fought in its line (handed over by one of them, in person): more for an
        // enemy champion cut down, and more with rank.
        out["reward"] = pay;
        out["rank"] = rankNow;
        out["rankTitle"] = fought ? fp->RankTitle(playerSideWas, rankNow) : std::string();
        out["promoted"] = promoted;
        out["championKills"] = enemyChampions;
        out["captainKills"] = enemyCaptains;
        lastPromotion_ = promoted;

        // What the player reads when it is over. (Nothing, when they are not there to see it.)
        if (playerLeft) {
            out["notification"] = "";
        } else if (skirmish) {
            out["notification"] = drawn ? std::string("The skirmish is over. Neither side had the better of it.")
                                        : "The skirmish is over. " + CapThe(victorName) + " had the better of it.";
        } else if (drawn) {
            out["notification"] = "The fighting dies down. Neither side holds the field at " + locationName + ".";
        } else if (fought && playerWon) {
            out["notification"] = "The day is won. The field at " + locationName + " belongs to " + The(victorName) + ".";
        } else if (fought) {
            out["notification"] = "The day is lost. The field at " + locationName + " belongs to " + The(victorName) + ".";
        } else {
            out["notification"] = "The fighting ends. The field at " + locationName + " belongs to " + The(victorName) + ".";
        }
        if (spectatorPenalized && !slighted.empty()) {
            out["spectatorNotification"] = CapThe(fp->GetFactionDisplayName(slighted)) + " will remember that you stood by.";
        }

        // People of an older game who were drawn into a battle side (3.8 enlisted guards) are let go.
        RestoreEnlistedGuards();

        // Don't remove player from battle faction here — Papyrus keeps the player in the
        // faction until deferred cleanup finishes (all soldiers disabled). Same-faction
        // membership prevents retaliation from friendly fire. No ForEachLoadedActor sweep needed.

        logger::info("Battle: Finalized — result={}, victor={}, playerSide={}, killsA={}, killsB={}, left={}, pay={}, rank {} -> {}{}",
            result, victor, playerSideWas, playerKillsA, playerKillsB, playerLeft, pay, rankBefore, rankNow,
            promoted.empty() ? std::string() : " (" + promoted + ")");
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BattleManager::CalculateReinforcementPositions(float playerX, float playerY, float playerZ,
                                                                float centerX, float centerY, int waveNum) {
        nlohmann::json out;

        int soldierCount = WAVE1_SOLDIERS;
        if (waveNum == 2) soldierCount = WAVE2_SOLDIERS;
        else if (waveNum == 3) soldierCount = WAVE3_SOLDIERS;
        else if (waveNum == 4) soldierCount = WAVE4_SOLDIERS;
        else if (waveNum >= 5) soldierCount = WAVE5_SOLDIERS;
        out["soldierCount"] = soldierCount;

        if (waveNum <= 1) {
            // Wave 1 uses rally markers, no special positioning
            out["useRallyMarkers"] = true;
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        // Direction AWAY from battle center (behind player relative to fight)
        float dx = playerX - centerX;
        float dy = playerY - centerY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.f) dist = 1.f;
        float dirX = dx / dist;
        float dirY = dy / dist;

        float behindX = playerX + dirX * REINFORCEMENT_BEHIND_DISTANCE;
        float behindY = playerY + dirY * REINFORCEMENT_BEHIND_DISTANCE;

        // Perpendicular offset with random variance
        float perpOffset = RandomFloat(200.f, 400.f);
        float perpX = -dirY * perpOffset;
        float perpY = dirX * perpOffset;

        out["useRallyMarkers"] = false;
        out["spawnAX"] = behindX + perpX;
        out["spawnAY"] = behindY + perpY;
        out["spawnBX"] = behindX - perpX;
        out["spawnBY"] = behindY - perpY;
        out["spawnZ"] = playerZ;

        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BattleManager::EvaluatePlayerJoin(const std::string& questAutoJoinFaction) {
        nlohmann::json out;
        out["shouldJoin"] = false;

        std::string factionA, factionB;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            factionA = activeBattle_->factionA;
            factionB = activeBattle_->factionB;
        }

        auto* fp = FactionPolitics::GetSingleton();

        // Quest-dispatched: player explicitly agreed
        if (!questAutoJoinFaction.empty()) {
            out["shouldJoin"] = true;
            out["joinFaction"] = questAutoJoinFaction;
            out["displayName"] = fp->GetFactionDisplayName(questAutoJoinFaction);
            out["isQuestJoin"] = true;
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        // Political: check standings
        int standingA = fp->GetPlayerStanding(factionA);
        int standingB = fp->GetPlayerStanding(factionB);

        if (standingA >= AUTO_JOIN_STANDING_THRESHOLD && standingB < AUTO_JOIN_STANDING_THRESHOLD) {
            out["shouldJoin"] = true;
            out["joinFaction"] = factionA;
            out["displayName"] = fp->GetFactionDisplayName(factionA);
            out["isQuestJoin"] = false;
        } else if (standingB >= AUTO_JOIN_STANDING_THRESHOLD && standingA < AUTO_JOIN_STANDING_THRESHOLD) {
            out["shouldJoin"] = true;
            out["joinFaction"] = factionB;
            out["displayName"] = fp->GetFactionDisplayName(factionB);
            out["isQuestJoin"] = false;
        }
        // Both high or neither high = spectator (no auto-join)

        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BattleManager::GetBattleNotification(const std::string& type,
                                                      const std::string& locationName,
                                                      const std::string& victorName,
                                                      bool playerWon) {
        int v = static_cast<int>(RandomFloat(0.f, 2.99f));

        if (type == "wave1") {
            const char* texts[] = {
                "The first soldiers take the field.",
                "Armed men move into position.",
                "Skirmishers advance ahead."
            };
            return texts[v];
        } else if (type == "wave2") {
            const char* texts[] = {
                "A second column appears on the road.",
                "More soldiers arrive to join the fight.",
                "Reinforcements close in from the flank."
            };
            return texts[v];
        } else if (type == "wave3") {
            const char* texts[] = {
                "The last reserves commit to the fight.",
                "Every remaining soldier enters the fray.",
                "One final group rushes in."
            };
            return texts[v];
        } else if (type == "no_survivor_win") {
            return "The field is yours \xe2\x80\x94 though none of your allies remain to celebrate.";
        } else if (type == "no_survivor_loss") {
            return "You stand alone among the fallen.";
        } else if (type == "soldier_victory") {
            const char* texts[] = {
                "Couldn't have done it without you, friend.",
                "You fight like a born warrior of Skyrim. The enemy never stood a chance.",
                "We'll drink to this victory tonight. You've earned it."
            };
            return texts[v];
        } else if (type == "soldier_defeat") {
            const char* texts[] = {
                "We lost the field... but we're still breathing. That counts for something.",
                "They broke our line, but not our spirit. We'll meet them again.",
                "They broke us... but at least we walk away."
            };
            return texts[v];
        }
        return "";
    }

    std::string BattleManager::ValidateFactionBattleDispatch(const std::string& alliedFaction,
                                                              const std::string& suggestedEnemy) {
        nlohmann::json out;
        out["canStart"] = false;

        auto* fp = FactionPolitics::GetSingleton();

        // If the DM suggested an enemy, validate it (exact or fuzzy match)
        std::string enemy;
        if (!suggestedEnemy.empty() && suggestedEnemy != alliedFaction) {
            // Exact match first
            std::string soldierTpl = fp->GetSoldierTemplate(suggestedEnemy);
            if (!soldierTpl.empty()) {
                enemy = suggestedEnemy;
                logger::info("BattleManager: Using DM-suggested enemy '{}' (exact match)", enemy);
            } else {
                // Fuzzy match: check all known factions for typos (Levenshtein ≤ 2)
                auto allFactions = fp->GetAllFactionIds();
                int bestDist = 999;
                std::string bestMatch;
                for (const auto& fid : allFactions) {
                    if (fid == alliedFaction) continue;
                    int dist = StringUtils::LevenshteinDistance(
                        StringUtils::ToLowerStd(suggestedEnemy),
                        StringUtils::ToLowerStd(fid));
                    if (dist < bestDist && dist <= 2) {
                        bestDist = dist;
                        bestMatch = fid;
                    }
                }
                if (!bestMatch.empty() && !fp->GetSoldierTemplate(bestMatch).empty()) {
                    enemy = bestMatch;
                    logger::info("BattleManager: Fuzzy matched DM enemy '{}' → '{}' (dist={})",
                        suggestedEnemy, enemy, bestDist);
                } else {
                    logger::warn("BattleManager: DM suggested '{}' — no match found, falling back", suggestedEnemy);
                }
            }
        }

        // Fall back to war enemy / configured rival
        if (enemy.empty()) {
            enemy = fp->GetFactionWarEnemy(alliedFaction);
        }

        if (enemy.empty() || enemy == alliedFaction) {
            out["failReason"] = "no war enemy for " + alliedFaction;
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        if (fp->IsDisbanded(alliedFaction) || fp->IsDisbanded(enemy)) {
            out["failReason"] = "one of the two is no more";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        // Two armies in the field means a war. (A "configured rival" used to do: the Companions drew up twenty-two
        // against the Silver Hand's stand-ins in a land at peace.)
        if (fp->GetActiveWarId(alliedFaction, enemy) <= 0) {
            out["failReason"] = alliedFaction + " and " + enemy + " are not at war";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                out["failReason"] = "battle system busy";
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
        }

        // Check scheduled battles too
        // (BattleScheduled is Papyrus state — caller must check separately)

        out["canStart"] = true;
        out["enemyFaction"] = enemy;
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BattleManager::CalculateMidBattleState(float scheduledTime, float currentTime) {
        nlohmann::json out;
        float elapsed = currentTime - scheduledTime;
        if (elapsed < 0.f) elapsed = 0.f;

        // Casualty rate: 15% per game hour elapsed, max 60%
        float casualtyRate = std::min(elapsed * 24.f * 0.15f, 0.6f);
        int casualties = static_cast<int>(WAVE1_SOLDIERS * casualtyRate);
        int soldiersPerSide = WAVE1_SOLDIERS - casualties;
        constexpr int MID_BATTLE_MIN = 3;
        if (soldiersPerSide < MID_BATTLE_MIN) soldiersPerSide = MID_BATTLE_MIN;

        // Morale loss proportional to casualties
        int moraleLoss = static_cast<int>(casualties * 4.f);
        if (moraleLoss > 40) moraleLoss = 40;

        out["soldiersPerSide"] = soldiersPerSide;
        out["moraleLoss"] = moraleLoss;
        out["casualties"] = casualties;
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BattleManager::GetPollAction(const std::string& stateJson) {
        nlohmann::json out;
        out["action"] = "none";

        if (stateJson.empty() || stateJson == "{}") {
            out["action"] = "battle_end_external";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        try {
            auto j = nlohmann::json::parse(stateJson);
            if (j.value("battle_over", std::string("false")) == "true") {
                out["action"] = "battle_end";
                out["result"] = j.value("result", "draw");
                out["victor"] = j.value("victor", "");
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }

            // Check wave spawn triggers
            if (j.contains("spawn_wave")) {
                out["action"] = "spawn_wave";
                out["waveNum"] = j["spawn_wave"].get<int>();
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
        } catch (const std::exception& e) {
            logger::error("GetPollAction: JSON parse error: {}", e.what());
            out["action"] = "error";
        }

        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void BattleManager::ResetBattleState() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Move actors to cleanup list before resetting (if not already moved by EndBattle)
            if (activeBattle_ && !activeBattle_->actors.empty()) {
                std::lock_guard<std::mutex> cLock(cleanupMutex_);
                if (cleanupFormIds_.empty()) {
                    for (const auto& a : activeBattle_->actors) {
                        cleanupFormIds_.push_back(a.formId);
                    }
                }
            }
            activeBattle_.reset();
        }
        suppressBattleBounty_ = false;
        logger::info("Battle: State reset");
    }

    void BattleManager::RestoreEnlistedGuards() {
        // Remove guards from battle factions and clear any combat against the player.
        auto [battleFactionA, battleFactionB] = ResolveBattleFactions();

        int restored = 0;
        for (auto fid : modifiedGuardFormIds_) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(fid);
            if (actor) {
                if (battleFactionA) actor->AddToFaction(battleFactionA, -1);
                if (battleFactionB) actor->AddToFaction(battleFactionB, -1);
                if (actor->IsInCombat()) {
                    actor->StopCombat();
                }
                restored++;
            }
        }
        modifiedGuardFormIds_.clear();
        if (restored > 0) {
            logger::info("Battle: Restored {} guards to pre-battle state", restored);
        }
    }

    void BattleManager::CleanupStaleBattleState() {
        // Safety net — called on game load to clean up guards/player from a battle
        // that ended while guards were unloaded or player cell-changed mid-battle.
        RestoreEnlistedGuards();
        RepairPlayerFactions();
        playerCrimeFactionsRemoved_ = false;
    }

    void BattleManager::RemovePlayerCrimeFactions() {
        // Nothing to do: see the header. The soldiers of a battle and of a faction's quest belong to no crime faction.
    }

    void BattleManager::RestorePlayerCrimeFactions() {
        // Nothing to do: see the header. (3.8: the player joined three factions here and Solitude's bounty was wiped.)
        playerCrimeFactionsRemoved_ = false;
    }

    void BattleManager::RepairPlayerFactions() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        // CrimeFactionHaafingar, TownIrontreeMillFaction, TownHalfMoonMillFaction: nobody's player belongs to these.
        for (const RE::FormID id : {RE::FormID(0x00029DB0), RE::FormID(0x00029DB1), RE::FormID(0x00029DB2)}) {
            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(id);
            if (!faction || !player->IsInFaction(faction)) continue;
            player->AddToFaction(faction, -1);
            logger::info("Battle: the player is taken out of {} ({:08X}), which an earlier version of this mod put them into",
                         faction->GetFormEditorID() ? faction->GetFormEditorID() : "a faction", id);
        }
    }

    std::string BattleManager::CalculateBattleMarkerPosition(float playerX, float playerY,
                                                              float locX, float locY, float locZ,
                                                              float offsetUnits) {
        float dx = locX - playerX;
        float dy = locY - playerY;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.f) dist = 1.f;

        float dirX = dx / dist;
        float dirY = dy / dist;
        float offsetX = dirX * offsetUnits;
        float offsetY = dirY * offsetUnits;

        nlohmann::json out;
        out["x"] = locX + offsetX;
        out["y"] = locY + offsetY;
        out["z"] = locZ;
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BattleManager::PickField(RE::Actor* player, RE::TESObjectREFR* place) {
        static FrameStats stats;
        FrameClock clock("Battle: looking for a field", stats, FrameClock::Now(), 20);
        nlohmann::json out;
        out["ok"] = false;
        const auto fail = [&out](const std::string& reason) {
            out["reason"] = reason;
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        };
        auto* tes = RE::TES::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        auto* world = player ? player->GetWorldspace() : nullptr;
        if (!tes || !cell || !world || cell->IsInteriorCell()) return fail("the player is indoors");
        // A town behind its walls is a small world of its own, hung onto the big one: no armies in its streets.
        if (world->parentWorld) return fail("the player is inside a walled town");

        // Everybody who is not of a battle: where they are, no battle is fought.
        auto [sideA, sideB] = ResolveBattleFactions();
        std::vector<Battlefield::Vec> people;
        // (Those the game runs in full around the player. The helper for "loaded actors" also walks the lists of people
        // the game only keeps track of, a thousand and more of them all over the world: by itself that took over a
        // millisecond here, and a stranger in another land with the same coordinates would have counted as near.)
        if (auto* lists = RE::ProcessLists::GetSingleton()) {
            for (const auto& handle : lists->highActorHandles) {
                const auto held = handle.get();
                auto* actor = held.get();
                if (!actor || actor == player || !actor->Is3DLoaded() || actor->IsDead() || actor->IsDisabled()) continue;
                if (actor->IsPlayerTeammate() || actor->GetWorldspace() != world) continue;
                if (!actor->HasKeywordString("ActorTypeNPC")) continue;
                if ((sideA && actor->IsInFaction(sideA)) || (sideB && actor->IsInFaction(sideB))) continue;
                people.push_back({actor->GetPositionX(), actor->GetPositionY()});
            }
        }

        const Battlefield::Vec me{player->GetPositionX(), player->GetPositionY()};
        const float facing = Battlefield::Norm(player->GetAngleZ() * kDegPerRad);
        const bool hasPlace = place && place != player && place->GetWorldspace() == world;
        const Battlefield::Vec placeAt = hasPlace ? Battlefield::Vec{place->GetPositionX(), place->GetPositionY()} : me;
        const float z = player->GetPositionZ();
        int lineSize = Battlefield::WaveSize(1);        // how many of a side are to be drawn up (fewer in a skirmish)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) lineSize = activeBattle_->lineSize;
        }
        std::string why = "no ground to stand on";
        for (const auto& c : Battlefield::Candidates(me, facing, hasPlace, placeAt)) {
            const auto field = Battlefield::Draw(c.centre, c.axis, 1, 1);
            const Ground mid = GroundAt(tes, c.centre, z), a = GroundAt(tes, field.a.centre, z), b = GroundAt(tes, field.b.centre, z);
            if (!mid.there || !a.there || !b.there) {
                why = "the ground there is not in the world";
                continue;
            }
            if (mid.wet || a.wet || b.wet) {
                why = "water";
                continue;
            }
            // Both lines, the middle, and the ground each line has to cross to get there.
            const Ground wayA = GroundAt(tes, Battlefield::Along(c.centre, c.axis, -Battlefield::kLineGap / 4.f), z);
            const Ground wayB = GroundAt(tes, Battlefield::Along(c.centre, c.axis, Battlefield::kLineGap / 4.f), z);
            if (!mid.walkable || !a.walkable || !b.walkable || !wayA.walkable || !wayB.walkable || wayA.wet || wayB.wet) {
                why = "no ground to walk on";
                continue;
            }
            if (std::fabs(a.height - b.height) > Battlefield::kMaxRise || std::fabs(mid.height - a.height) > Battlefield::kMaxRise ||
                std::fabs(mid.height - b.height) > Battlefield::kMaxRise) {
                why = "too steep";
                continue;
            }
            // Every place somebody is to stand at, not the middles alone: a stream runs where the middle is dry.
            // (Height and water only: two cheap questions a spot, asked of the few places that got this far.)
            {
                const int size = lineSize;
                const auto lines = Battlefield::Draw(c.centre, c.axis, size, size);
                bool standsInWater = false;
                for (const auto* line : {&lines.a, &lines.b}) {
                    for (const auto& spot : line->spots) {
                        const RE::NiPoint3 pos{spot.x, spot.y, z};
                        float height = 0.f;
                        auto* spotCell = tes->GetCell(pos);
                        if (!spotCell || !tes->GetLandHeight(pos, height) || Battlefield::Wet(tes->GetWaterHeight(pos, spotCell), height)) {
                            standsInWater = true;
                            break;
                        }
                    }
                    if (standsInWater) break;
                }
                if (standsInWater) {
                    why = "water where the lines would stand";
                    continue;
                }
            }
            bool crowded = false;
            for (const auto& p : people) {
                if (Battlefield::Dist(p, c.centre) < Battlefield::kClearOfPeople) {
                    crowded = true;
                    break;
                }
            }
            if (crowded) {
                why = "people about";
                continue;
            }
            if (DoorNear(tes, c.centre, Battlefield::kClearOfHouses)) {
                why = "houses";
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (activeBattle_) {
                    activeBattle_->fieldSet = true;
                    activeBattle_->fieldX = c.centre.x;
                    activeBattle_->fieldY = c.centre.y;
                    activeBattle_->fieldZ = mid.height;
                    activeBattle_->fieldAxis = c.axis;
                }
            }
            out["ok"] = true;
            out["x"] = c.centre.x;
            out["y"] = c.centre.y;
            out["z"] = mid.height;
            out["axis"] = c.axis;
            logger::info("Battle: a field {:.0f} units from the player ({}), lines along {:.0f} degrees; {} people about, none near it",
                         Battlefield::Dist(me, c.centre), c.inView ? "where they look" : "out of their view", c.axis, people.size());
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        logger::info("Battle: no field near the player ({})", why);
        return fail(why);
    }

    std::string BattleManager::MusterLine(const std::string& side, int first, int count, RE::Actor* player) {
        static FrameStats stats;
        FrameClock clock("Battle: drawing up soldiers", stats, FrameClock::Now(), 60);
        nlohmann::json out;
        out["success"] = false;
        out["count"] = 0;
        const bool isA = IsA(side);
        std::string factionId;
        Battlefield::Vec centre;
        float axis = 0.f, z = 0.f;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || !activeBattle_->fieldSet || !player) {
                out["error"] = "no battle with a field";
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            factionId = isA ? activeBattle_->factionA : activeBattle_->factionB;
            centre = {activeBattle_->fieldX, activeBattle_->fieldY};
            axis = activeBattle_->fieldAxis;
            z = activeBattle_->fieldZ;
        }
        auto [battleFactionA, battleFactionB] = ResolveBattleFactions();
        if (!battleFactionA || !battleFactionB) {
            out["error"] = "battle factions not found in IntelEngine.esp";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        int total = Battlefield::WaveSize(1);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) total = activeBattle_->lineSize;
        }
        first = std::clamp(first, 0, total);
        count = std::clamp(count, 0, total - first);
        // Whoever is left in a battle side from before is the player only by mistake: the two are enemies, and a
        // player in both is attacked by everyone.
        if (isA && first == 0) {
            player->AddToFaction(battleFactionA, -1);
            player->AddToFaction(battleFactionB, -1);
        }

        const auto field = Battlefield::Draw(centre, axis, total, total);
        const auto& line = isA ? field.a : field.b;
        const std::vector<Battlefield::Vec> spots(line.spots.begin() + first, line.spots.begin() + first + count);
        const auto who = NextOf(isA, factionId, first, count, total, player);
        const auto soldiers = RaiseSoldiers(factionId, isA ? battleFactionA : battleFactionB, spots, line.facing, z, player, true, first == 0, true, &who);
        nlohmann::json ids = nlohmann::json::array();
        for (auto* s : soldiers) ids.push_back(s->GetFormID());
        out["success"] = true;
        out["count"] = static_cast<int>(soldiers.size());
        out["formIds"] = ids;
        if (first == 0 && !soldiers.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) (isA ? activeBattle_->commanderA : activeBattle_->commanderB) = soldiers[0]->GetFormID();
            out["commander"] = nlohmann::json::array({soldiers[0]->GetFormID()});
        }
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string BattleManager::ReinforceLine(const std::string& side, int first, int count, RE::Actor* player, int kind) {
        const bool flank = kind == 1;
        static FrameStats stats;
        FrameClock clock("Battle: bringing up soldiers", stats, FrameClock::Now(), 60);
        nlohmann::json out;
        out["success"] = false;
        out["count"] = 0;
        out["formIds"] = nlohmann::json::array();
        const bool isA = IsA(side);
        std::string factionId;
        Battlefield::Vec centre, point;
        float axis = 0.f, z = 0.f;
        int have = 0;
        bool bringsChampion = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || !activeBattle_->fieldSet || !player) {
                out["error"] = "no battle with a field";
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            factionId = isA ? activeBattle_->factionA : activeBattle_->factionB;
            centre = {activeBattle_->fieldX, activeBattle_->fieldY};
            axis = activeBattle_->fieldAxis;
            z = activeBattle_->fieldZ;
            point = flank ? Battlefield::Vec{activeBattle_->flankX, activeBattle_->flankY}
                  : isA ? Battlefield::Vec{activeBattle_->reserveAX, activeBattle_->reserveAY}
                        : Battlefield::Vec{activeBattle_->reserveBX, activeBattle_->reserveBY};
            for (const auto& a : activeBattle_->actors) {
                if (a.factionId == factionId) ++have;
            }
            // A side's champion comes up alone (once).
            bringsChampion = kind == 2 && (isA ? activeBattle_->championA : activeBattle_->championB) == 0;
        }
        if (kind == 2 && !bringsChampion) {
            out["success"] = true;
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        // (The hidden force and a champion are over and above what a side brings up in its waves.)
        if (kind == 2) {
            first = 0;
            count = 1;
        } else {
            count = (std::min)(count, MAX_SOLDIERS_PER_SIDE + (flank ? Battlefield::kFlankSize : 0) - have);
        }
        if (count <= 0) {
            out["success"] = true;           // everybody this side has is on the field already
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        auto [battleFactionA, battleFactionB] = ResolveBattleFactions();
        auto* tes = RE::TES::GetSingleton();
        if (!battleFactionA || !battleFactionB || !tes) {
            out["error"] = "battle factions not found in IntelEngine.esp";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        const auto field = Battlefield::Draw(centre, axis, 1, 1);
        const auto& line = isA ? field.a : field.b;
        const Battlefield::Vec me{player->GetPositionX(), player->GetPositionY()};
        if (first <= 0) {
            // Where this group comes from: behind their own line, where the player is least likely to see it.
            // The first of the places worth trying that can be walked from. None (the player stands where they would
            // come from, or it is all rock and water there): nobody comes this time, rather than out of thin air next
            // to the player (it happened: 685 units off, in plain view, 2026-11-20).
            bool found = false;
            const float looking = Battlefield::Norm(player->GetAngleZ() * kDegPerRad);
            // Those who come up come from behind their own line; a hidden force from out to one side of the other's.
            const auto places = flank ? Battlefield::FlankPoints(isA ? field.b : field.a, me, looking) : Battlefield::ReservePoints(line, me, looking);
            for (const auto& at : places) {
                const Ground ground = GroundAt(tes, at, z);
                if (!ground.walkable || ground.wet) continue;
                point = at;
                found = true;
                break;
            }
            if (!found) {
                logger::info("Battle: nowhere for more of {} to come from unseen: nobody comes up this time", factionId);
                out["success"] = true;
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
            logger::info("Battle: more of {} come {}, {:.0f} units from the player", factionId,
                         flank ? "out of hiding on the flank" : "up behind their line", Battlefield::Dist(me, point));
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_ && flank) {
                activeBattle_->flankX = point.x;
                activeBattle_->flankY = point.y;
            } else if (activeBattle_) {
                (isA ? activeBattle_->reserveAX : activeBattle_->reserveBX) = point.x;
                (isA ? activeBattle_->reserveAY : activeBattle_->reserveBY) = point.y;
            }
        }
        std::vector<Battlefield::Vec> spots;
        for (int i = 0; i < count; ++i) spots.push_back(Battlefield::ReserveSpot(point, line.facing, (std::max)(first, 0) + i));

        // (A champion is rolled by themself, so that a wave's group being made at the same time is left as it is.)
        const auto who = kind == 2 ? RollFor(factionId, 1, player)
                                   : NextOf(isA, factionId, first, count, (std::max)(count, flank ? Battlefield::kFlankSize : Battlefield::WaveSize(GetWave(side) + 1)), player);
        // A hidden force looks at the line it falls on.
        const float looks = flank ? Battlefield::Bearing(point, (isA ? field.b : field.a).centre) : line.facing;
        const auto soldiers = RaiseSoldiers(factionId, isA ? battleFactionA : battleFactionB, spots, looks, z, player, false, false, true, &who, bringsChampion);
        nlohmann::json ids = nlohmann::json::array();
        for (auto* s : soldiers) ids.push_back(s->GetFormID());
        out["count"] = static_cast<int>(soldiers.size());
        if (bringsChampion && !soldiers.empty()) {
            out["champion"] = nlohmann::json::array({soldiers[0]->GetFormID()});
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) (isA ? activeBattle_->championA : activeBattle_->championB) = soldiers[0]->GetFormID();
            logger::info("Battle: a champion of {} comes up", factionId);
        }
        out["formIds"] = ids;
        {
            // The battle may have ended while they were made: they are switched off, and the script removes them
            // by the ids it gets back.
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) {
                out["error"] = "battle ended during spawn";
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
        }
        out["success"] = true;
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void BattleManager::SetSkirmish(int size) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;
        activeBattle_->skirmish = true;
        activeBattle_->twists = Battlefield::Twists{};       // a skirmish is what it looks like
        activeBattle_->lineSize = std::clamp(size, 2, 6);
        activeBattle_->waves = 1;
        // A handful breaks when one of them is left standing. (Counted by the head it took the last man's death:
        // four against four was fought until one side had nobody, 2026-11-20.)
        const int others = (std::max)(1, activeBattle_->lineSize - 1);
        activeBattle_->lossPerSoldier = (75 + others - 1) / others;
        logger::info("[BattleManager] Battle {} is a skirmish of {} a side", activeBattle_->id, activeBattle_->lineSize);
    }

    void BattleManager::StripFactions(RE::Actor* actor) { StripOwnFactions(actor); }

    RE::Actor* BattleManager::RaiseLoner(const std::string& factionId, Battlefield::Vec at, float facingDeg, RE::Actor* player) {
        static FrameStats stats;
        FrameClock clock("Battle: making one who comes alone", stats, FrameClock::Now(), 20);
        const auto made = RaiseSoldiers(factionId, nullptr, {at}, facingDeg, player ? player->GetPositionZ() : 0.f, player, true, false, false);
        return made.empty() ? nullptr : made.front();
    }

    bool BattleManager::PickApproach(RE::Actor* player, RE::Actor* target, Battlefield::Vec& at) const {
        auto* tes = RE::TES::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        if (!tes || !cell || cell->IsInteriorCell() || !target) return false;
        const Battlefield::Vec me{player->GetPositionX(), player->GetPositionY()};
        const Battlefield::Vec them{target->GetPositionX(), target->GetPositionY()};
        const float facing = Battlefield::Norm(player->GetAngleZ() * kDegPerRad);
        float best = -1.f;
        for (const float distance : {2600.f, 3400.f}) {
            for (float bearing = 0.f; bearing < 360.f; bearing += 45.f) {
                const Battlefield::Vec spot = Battlefield::Along(them, bearing, distance);
                const float fromMe = Battlefield::Dist(me, spot);
                const bool inView = Battlefield::Apart(Battlefield::Bearing(me, spot), facing) <= Battlefield::kViewHalfAngle;
                if (fromMe < Battlefield::kReserveClear || fromMe > 7600.f || (inView && fromMe < Battlefield::kNearInView)) continue;
                const Ground ground = GroundAt(tes, spot, player->GetPositionZ());
                if (!ground.walkable || ground.wet) continue;
                if (fromMe > best) {
                    best = fromMe;
                    at = spot;
                }
            }
        }
        return best > 0.f;
    }

    std::string BattleManager::PickAmbush(RE::Actor* player) const {
        nlohmann::json out;
        out["ok"] = false;
        auto* tes = RE::TES::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        auto* world = player ? player->GetWorldspace() : nullptr;
        if (!tes || !cell || !world || cell->IsInteriorCell()) {
            out["reason"] = "the player is indoors";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        if (world->parentWorld) {
            out["reason"] = "the player is inside a walled town";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        const Battlefield::Vec me{player->GetPositionX(), player->GetPositionY()};
        if (DoorNear(tes, me, Battlefield::kClearOfHouses)) {
            out["reason"] = "houses";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        Battlefield::Vec at;
        if (!PickApproach(player, player, at)) {
            out["reason"] = "nowhere to come from unseen";
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        float height = player->GetPositionZ();
        tes->GetLandHeight(RE::NiPoint3{at.x, at.y, height}, height);
        out["ok"] = true;
        out["x"] = at.x;
        out["y"] = at.y;
        out["z"] = height;
        logger::info("Battle: a band can come for the player from {:.0f} units off, out of their sight", Battlefield::Dist(me, at));
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    RE::TESFaction* BattleManager::EnemySide() const { return ResolveBattleFactions().second; }

    std::string BattleManager::TurnCoats(const std::string& side) {
        nlohmann::json out;
        nlohmann::json ids = nlohmann::json::array();
        out["count"] = 0;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) {
            out["formIds"] = ids;
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        auto& battle = *activeBattle_;
        const bool isA = IsA(side);
        const std::string& from = isA ? battle.factionA : battle.factionB;
        const std::string& to = isA ? battle.factionB : battle.factionA;
        int alive = 0;
        for (const auto& a : battle.actors) {
            if (a.alive && a.factionId == from) ++alive;
        }
        int turn = Battlefield::Turncoats(alive);
        int turned = 0;
        // The last who came are the first to go: not the captain, not a champion.
        for (auto it = battle.actors.rbegin(); it != battle.actors.rend() && turned < turn; ++it) {
            if (!it->alive || it->factionId != from || it->tier != 0) continue;
            it->factionId = to;
            ids.push_back(it->formId);
            ++turned;
        }
        if (turned > 0) {
            int& morale = isA ? battle.moraleA : battle.moraleB;
            morale = std::clamp(morale - Battlefield::kLossBetrayed, 0, Battlefield::kHeartMost);
            logger::info("[BattleManager] Battle {}: {} of {} turn their coats and fight for {}", battle.id, turned, from, to);
        }
        out["count"] = turned;
        out["formIds"] = ids;
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void BattleManager::SetTwists(int champion, int flank, int turncoat) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;
        activeBattle_->twists.champion = std::clamp(champion, 0, 3);
        activeBattle_->twists.flank = std::clamp(flank, 0, 2);
        activeBattle_->twists.turncoat = std::clamp(turncoat, 0, 2);
        activeBattle_->twists.flankAfter = 20.f;
        activeBattle_->twists.championAfter = 30.f;
        logger::info("[BattleManager] Battle {} (test): champion {}, hidden force {}, turncoats {}", activeBattle_->id, champion, flank, turncoat);
    }

    void BattleManager::BeginFight() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_ || !activeBattle_->mustering) return;
        activeBattle_->mustering = false;
        activeBattle_->fightBegan = RealSeconds();
        logger::info("[BattleManager] Battle {}: the lines meet", activeBattle_->id);
    }

    bool BattleManager::IsMustering() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeBattle_ && activeBattle_->mustering;
    }

    std::string BattleManager::Verdict() const {
        nlohmann::json out;
        out["result"] = "draw";
        out["victor"] = "";
        std::string factionA, factionB;
        int winner = 0;
        bool unseen = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            factionA = activeBattle_->factionA;
            factionB = activeBattle_->factionB;
            unseen = activeBattle_->unseen;
            int aliveA = 0, aliveB = 0;
            for (const auto& a : activeBattle_->actors) {
                if (!a.alive) continue;
                if (a.factionId == factionA) ++aliveA;
                else ++aliveB;
            }
            winner = activeBattle_->mustering ? 0 : Battlefield::Verdict(aliveA, aliveB, activeBattle_->moraleA, activeBattle_->moraleB);
        }
        // Nobody had the better of it when the player left: with nobody watching it is fought out all the same,
        // and the side that stands better in the war has the better chance. (Two armies that are seen to draw off
        // are a drawn battle; two that nobody watched are not.)
        if (winner == 0 && unseen) {
            if (const auto war = PoliticalDB::GetSingleton()->GetActiveWar(factionA, factionB)) {
                const bool aIsWarA = factionA == war->factionA;
                const float scoreA = static_cast<float>(aIsWarA ? war->factionAMorale + war->factionAStrength / 2 : war->factionBMorale + war->factionBStrength / 2) + RandomFloat(0.f, 45.f);
                const float scoreB = static_cast<float>(aIsWarA ? war->factionBMorale + war->factionBStrength / 2 : war->factionAMorale + war->factionAStrength / 2) + RandomFloat(0.f, 45.f);
                winner = scoreA >= scoreB ? 1 : 2;
            }
        }
        if (winner == 1) {
            out["result"] = factionA + "_victory";
            out["victor"] = factionA;
        } else if (winner == 2) {
            out["result"] = factionB + "_victory";
            out["victor"] = factionB;
        }
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    void BattleManager::NoteTurnedDown(const std::string& factionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (activeBattle_ && (factionId == activeBattle_->factionA || factionId == activeBattle_->factionB)) activeBattle_->turnedDown.insert(factionId);
    }

    void BattleManager::NotePlayerLeft() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;
        activeBattle_->unseen = true;
        if (!activeBattle_->playerSide.empty()) activeBattle_->playerLeft = true;
    }

    std::string BattleManager::Say(const std::string& asked, const std::string& a, const std::string& b, const std::string& name, int number,
                                   int other) const {
        const std::string what = Key(asked);
        auto* fp = FactionPolitics::GetSingleton();
        std::string place;
        bool fighting = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                place = activeBattle_->locationName;
                fighting = !activeBattle_->mustering;
            } else {
                place = lastPlace_;         // the battle is over: its place is still what is spoken of
            }
        }
        if (place.empty()) place = "here";
        const std::string theA = a.empty() ? std::string() : The(fp->GetFactionDisplayName(a));
        const std::string theB = b.empty() ? std::string() : The(fp->GetFactionDisplayName(b));

        // ---- what the player reads
        if (what == "drawn_up") return "Two armies are drawn up near " + place + ": " + theA + " and " + theB + ".";
        if (what == "ambush") return "Men of " + theA + " are coming for you.";
        // A rank's name with its article: "a Veteran", "an Auxiliary".
        const auto aRank = [](const std::string& title) {
            if (title.empty()) return std::string();
            const char first = static_cast<char>(std::tolower(static_cast<unsigned char>(title[0])));
            return std::string(first == 'a' || first == 'e' || first == 'i' || first == 'o' || first == 'u' ? "an " : "a ") + title;
        };
        if (what == "champion_name") return "Champion of " + theA;
        if (what == "champion_comes") return "A champion of " + theA + " takes the field.";
        if (what == "champion_fell") return "The champion of " + theA + " has fallen.";
        if (what == "flank") return "A second force of " + theA + " breaks from cover on the flank!";
        if (what == "treachery") return "Treachery! Men of " + theA + " turn on their own.";
        // What a man who has turned his coat is called from then on: he still wears his old side's armour, and with
        // his old name on him nobody could tell him from the comrades he is cutting down (seen 2026-11-21).
        if (what == "turncoat_name") return "Turncoat";
        if (what == "promoted") return CapThe(fp->GetFactionDisplayName(a)) + " now count you " + aRank(name) + ".";      // name: the rank
        if (what == "file") return std::string(number >= 4 ? "Four" : "Two") + " soldiers of " + theA + " keep by your side.";
        if (what == "word_body") return "The line of " + theA + " waits for your word, " + name + ".";            // name: the rank
        if (what == "word_charge") return "Charge!";
        if (what == "word_hold") return "Hold a little longer";
        if (what == "champion_cry") {
            return "is the champion of " + theA + ", come up into the battle against " + theB + " near " + place +
                   "; bellows a challenge of a few words across the field" + (name.empty() ? std::string() : ", at " + name + " above all");
        }
        if (what == "turncoat_cry") {
            return "has just turned on their own side, " + theA + ", in the middle of the battle near " + place + ", and fights for " + theB +
                   " now; shouts a few words of why, or of scorn, at their old comrades";
        }
        if (what == "skirmish_met") return "Men of " + theA + " and of " + theB + " have run into each other near " + place + ".";
        if (what == "broke_out") return "Fighting has broken out near " + place + ".";
        if (what == "stand_with") return "You stand with " + theA + ".";
        if (what == "captain_fell") return "The captain of " + theA + " has fallen.";
        if (what == "more_come") return "More soldiers of " + theA + " come up.";
        if (what == "last_come") return "The last of " + theA + " come up.";
        if (what == "word_won") return "Word reaches you: " + theA + " won the battle near " + name + ".";      // name: the place
        if (what == "word_nobody") return "Word reaches you: neither side held the field near " + name + ".";
        if (what == "choice_body") {
            return std::string(fighting ? "Near " + place + ", " + theA + " and " + theB + " are fighting. "
                                        : "Near " + place + ", " + theA + " and " + theB + " are about to fight. ") +
                   name + " calls you to their line.";
        }
        if (what == "choice_join") return "Stand with " + theA;
        if (what == "choice_out") return "Stay out of it";
        if (what == "commander_name") return fp->GetFactionDisplayName(a) + " Captain";
        if (what == "stranger") return "Stranger";

        // ---- what somebody is given to say, in their own words (told to the model; a: their side, b: the other)
        if (what == "hail") {
            // number: 1 when their side counts this person a friend.
            return "leads the soldiers of " + theA +
                   (fighting ? ", in the thick of a battle against " + theB + " near " + place
                             : ", drawn up for battle against " + theB + " near " + place + ", moments before the lines meet") +
                   ", sees " + name + " close by (" +
                   (fp->PlayerRank(a) > 0 ? "whom they know as " + aRank(fp->RankTitle(a, fp->PlayerRank(a))) + " of their own, and greet as that"
                    : number > 0 ? std::string("whom they know as a friend of their side") : std::string("a stranger to them, but another blade is welcome")) +
                   ") and calls out to them, in a few words and as a soldier would: will they stand with " + theA +
                   " today, or keep out of the way?";
        }
        if (what == "warn_off") {
            // They know this person as an officer of the army across the field.
            return "leads the soldiers of " + theA + ", drawn up for battle against " + theB + " near " + place + ", moments before the lines meet, sees " +
                   name + " come near their line, whom they know as " + aRank(fp->RankTitle(b, fp->PlayerRank(b))) + " of " + theB +
                   ", the enemy they are about to fight; shouts a few words at them across the gap, of scorn or of warning, as a soldier would to an "
                   "enemy officer who walks up to the wrong line";
        }
        if (what == "cry") {
            return "leads the soldiers of " + theA + ", drawn up for battle against " + theB + " near " + place +
                   ", and now gives the word to charge: a battle cry of a few words, shouted to the line";
        }
        if (what == "after_won") {
            // number: enemies this person cut down; other: the gold they are paid.
            std::string told = "has just won the battle near " + place + " against " + theB + ", with " + name + " fighting in the same line";
            if (number > 0) told += " (" + name + " cut down " + std::to_string(number) + " of the enemy)";
            told += ", and comes over, weary and glad, to thank them in a few words";
            if (other > 0) told += " and to press their share of the pay into their hand: " + std::to_string(other) + " gold";
            if (!lastPromotion_.empty()) {
                told += ". Tells them, too, that from today they are counted " + aRank(lastPromotion_) + " of " + theA;
                lastPromotion_.clear();
            }
            return told;
        }
        if (what == "after_drawn") {
            return "has fought near " + place + " against " + theB + " until neither side could go on, with " + name +
                   " in the same line, and comes over, spent, for a few words before what is left of them draws off";
        }
        if (what == "after_lost") {
            return "has just lost the battle near " + place + " against " + theB + ", with " + name +
                   " fighting in the same line, and comes over, out of breath, to tell them in a few words to get away while they can";
        }
        return {};
    }

    std::string BattleManager::SideOpenTo(RE::Actor* actor) const {
        if (!actor) return {};
        std::string own;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || !activeBattle_->playerSide.empty()) return {};
            const auto id = actor->GetFormID();
            for (const auto& a : activeBattle_->actors) {
                if (a.formId == id && a.alive) {
                    own = a.factionId;
                    break;
                }
            }
        }
        if (own.empty()) return {};
        auto* fp = FactionPolitics::GetSingleton();
        return Battlefield::WouldHave(fp->GetPlayerStanding(own)) ? fp->GetFactionDisplayName(own) : std::string();
    }

    std::string BattleManager::SoldierBrief(RE::Actor* actor) const {
        if (!actor) return {};
        std::string own, foe, place, playerSide;
        bool mustering = false, leads = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) return {};
            const auto id = actor->GetFormID();
            for (const auto& a : activeBattle_->actors) {
                if (a.formId == id && a.alive) {
                    own = a.factionId;
                    leads = a.tier == 2;
                    break;
                }
            }
            if (own.empty()) return {};
            foe = own == activeBattle_->factionA ? activeBattle_->factionB : activeBattle_->factionA;
            place = activeBattle_->locationName;
            playerSide = activeBattle_->playerSide;
            mustering = activeBattle_->mustering;
        }
        auto* fp = FactionPolitics::GetSingleton();
        auto* player = RE::PlayerCharacter::GetSingleton();
        const std::string name = player ? player->GetDisplayFullName() : "the traveller";
        std::string line = leads ? "Right now you lead the soldiers of " : "Right now you are a soldier of ";
        line += The(fp->GetFactionDisplayName(own));
        line += mustering ? ", drawn up in line of battle against " : ", in the middle of a battle against ";
        line += The(fp->GetFactionDisplayName(foe)) + " near " + place + ". ";
        line += mustering ? "The lines have not met yet; it will begin at any moment." : "The fighting is on.";
        if (playerSide == own) {
            const int rank = fp->PlayerRank(own);
            line += rank > 0 ? " " + name + ", known to you all as " + fp->RankTitle(own, rank) + " of your side, fights in your line."
                             : " " + name + " fights in your line.";
        } else if (playerSide == foe) {
            line += " " + name + " fights for the enemy.";
        } else if (Battlefield::WouldHave(fp->GetPlayerStanding(own))) {
            line += " " + name + " has taken no side. If they say they will fight on yours, they are welcome in your line.";
        } else {
            line += " Your side knows " + name + " for an enemy.";
        }
        return line;
    }

    std::string BattleManager::CallerOf(const std::string& factionA, const std::string& factionB) {
        auto* fp = FactionPolitics::GetSingleton();
        const int who = Ranks::Caller(fp->PlayerRank(factionA), fp->PlayerRank(factionB));
        return who == 1 ? factionA : who == 2 ? factionB : std::string();
    }

    RE::TESObjectREFR* BattleManager::WayHome(const std::string& factionId, Battlefield::Vec field, Battlefield::Vec away, bool beaten, RE::FormID notTo) {
        static FrameStats stats;
        FrameClock clock("Battle: where an army marches off to", stats, FrameClock::Now(), 20);
        // What its own call home: the place named in its entry (a town, or a hold: then any town of that hold).
        std::string home;
        if (const auto faction = FactionPolitics::GetSingleton()->GetFaction(factionId)) home = faction->hold;
        const auto same = [](const char* name, const std::string& wanted) {
            if (!name || wanted.empty() || std::strlen(name) != wanted.size()) return false;
            for (std::size_t i = 0; i < wanted.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(name[i])) != std::tolower(static_cast<unsigned char>(wanted[i]))) return false;
            }
            return true;
        };
        std::vector<Battlefield::Town> towns;
        for (const auto& mark : LivingWorldBridge::GetSingleton()->Towns()) {
            const auto* marker = RE::TESForm::LookupByID<RE::TESObjectREFR>(mark.marker);
            if (!marker) continue;
            Battlefield::Town town;
            town.at = {marker->GetPositionX(), marker->GetPositionY()};
            town.id = mark.marker;
            if (!home.empty()) {
                const auto* location = RE::TESForm::LookupByID<RE::BGSLocation>(mark.location);
                const auto* hold = RE::TESForm::LookupByID<RE::BGSLocation>(mark.hold);
                town.home = same(location ? location->GetFullName() : nullptr, home) || same(hold ? hold->GetFullName() : nullptr, home);
            }
            towns.push_back(town);
        }
        const int pick = Battlefield::MarchTo(towns, field, away, beaten, notTo);
        auto* to = pick < 0 ? nullptr : RE::TESForm::LookupByID<RE::TESObjectREFR>(towns[static_cast<std::size_t>(pick)].id);
        if (to) {
            const auto& town = towns[static_cast<std::size_t>(pick)];
            const auto* location = to->GetCurrentLocation();
            logger::info("Battle: {} {} toward {} ({:.0f} units off{}; {} towns known)", factionId, beaten ? "run" : "march off",
                         location && location->GetFullName() ? location->GetFullName() : "a town", Battlefield::Dist(field, town.at),
                         town.home ? ", their home" : "", towns.size());
        } else {
            logger::info("Battle: no town for {} to march off to ({} known): a point far off instead", factionId, towns.size());
        }
        return to;
    }

    std::string BattleManager::Call() {
        // (Copied out first: asking the ladder takes the politics locks, never under the gatherings' own.)
        std::vector<PendingBattle> pending;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pending = pendingBattles_;
        }
        for (auto& pb : pending) {
            const auto who = CallerOf(pb.factionA, pb.factionB);
            if (who.empty()) continue;
            if (!pb.marker) {
                // Where it is: the place's own map marker, as when the gathering was made.
                auto* resolver = LocationResolver::GetSingleton();
                std::string name = pb.locationName;
                auto* location = resolver->ResolveLocation(name);
                auto* mark = location ? std::as_const(location->worldLocMarker).get().get() : nullptr;
                if (!mark) mark = resolver->FindTravelTarget(pb.locationName);
                if (!mark) continue;
                pb.marker = mark->GetFormID();
                pb.location = location ? location->GetFormID() : 0;
                std::lock_guard<std::mutex> lock(pendingMutex_);
                for (auto& kept : pendingBattles_) {
                    if (kept.id == pb.id) {
                        kept.marker = pb.marker;
                        kept.location = pb.location;
                    }
                }
            }
            nlohmann::json out;
            out["id"] = pb.id;
            out["faction"] = who;
            out["place"] = pb.locationName;
            out["location"] = nlohmann::json::array({pb.location});
            out["marker"] = nlohmann::json::array({pb.marker});
            return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        return "{}";
    }

    void BattleManager::SetCallText(const std::string& factionId, const std::string& place) {
        auto* quest = RE::TESForm::LookupByEditorID<RE::TESQuest>("IntelEngine_Call");
        if (!quest) return;
        auto* fp = FactionPolitics::GetSingleton();
        const std::string text = "Stand with " + The(fp->GetFactionDisplayName(factionId)) + " near " + place + " before the battle is decided";
        for (auto* objective : quest->objectives) {
            if (objective && objective->index == 0) objective->displayText = text.c_str();
        }
    }

    nlohmann::json BattleManager::DashboardJson(float nowGameDays) const {
        nlohmann::json out = nlohmann::json::object();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                const auto& b = *activeBattle_;
                int standA = 0, standB = 0;
                for (const auto& a : b.actors) {
                    if (a.alive) ++(a.factionId == b.factionA ? standA : standB);
                }
                nlohmann::json j;
                j["a"] = b.factionA;
                j["b"] = b.factionB;
                j["place"] = b.locationName;
                j["phase"] = b.mustering ? "muster" : "fight";
                j["skirmish"] = b.skirmish;
                j["morale_a"] = (std::min)(b.moraleA, Battlefield::kHeartFull);      // shown as a share of a whole heart
                j["morale_b"] = (std::min)(b.moraleB, Battlefield::kHeartFull);
                j["standing_a"] = standA;
                j["standing_b"] = standB;
                j["player_side"] = b.playerSide;
                j["player_kills"] = b.playerKillsA + b.playerKillsB;
                out["battle"] = std::move(j);
            }
        }
        nlohmann::json gatherings = nlohmann::json::array();
        std::vector<PendingBattle> pending;
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pending = pendingBattles_;
        }
        for (const auto& pb : pending) {
            nlohmann::json j;
            j["a"] = pb.factionA;
            j["b"] = pb.factionB;
            j["place"] = pb.locationName;
            j["hold"] = pb.hold;
            j["hours_left"] = (std::max)(0.f, (pb.deadline - nowGameDays) * 24.f);
            if (const auto who = CallerOf(pb.factionA, pb.factionB); !who.empty()) j["calls"] = who;
            gatherings.push_back(std::move(j));
        }
        out["gatherings"] = std::move(gatherings);
        return out;
    }

    std::string BattleManager::FieldInfo() const {
        nlohmann::json out;
        std::lock_guard<std::mutex> lock(mutex_);
        out["set"] = (activeBattle_ && activeBattle_->fieldSet) ? "true" : "false";
        if (activeBattle_ && activeBattle_->fieldSet) {
            out["x"] = activeBattle_->fieldX;
            out["y"] = activeBattle_->fieldY;
            out["z"] = activeBattle_->fieldZ;
            out["axis"] = activeBattle_->fieldAxis;
            out["mustering"] = activeBattle_->mustering ? "true" : "false";
            out["commanderA"] = nlohmann::json::array({activeBattle_->commanderA});
            out["commanderB"] = nlohmann::json::array({activeBattle_->commanderB});
        }
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    // =========================================================================
    // Shared Helpers
    // =========================================================================

    // SetActorPlayerTeammate — REMOVED. kPlayerTeammate causes cascade attacks
    // (allies defend player against retaliating ally → infighting → guards join).
    // Battle faction membership + SetBeenAttacked(false) handles friendly fire.

    std::pair<RE::TESFaction*, RE::TESFaction*> BattleManager::ResolveBattleFactions() const {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) return {nullptr, nullptr};
        auto* modFile = dataHandler->LookupModByName("IntelEngine.esp");
        if (!modFile) return {nullptr, nullptr};

        bool isLight = modFile->IsLight();
        RE::FormID baseA = isLight ? ((modFile->GetPartialIndex() << 12) | 0x36B)
                                   : ((modFile->GetCompileIndex() << 24) | 0x00536B);
        RE::FormID baseB = isLight ? ((modFile->GetPartialIndex() << 12) | 0x36C)
                                   : ((modFile->GetCompileIndex() << 24) | 0x00536C);
        return {
            RE::TESForm::LookupByID<RE::TESFaction>(baseA),
            RE::TESForm::LookupByID<RE::TESFaction>(baseB)
        };
    }

    float BattleManager::RandomFloat(float min, float max) {
        thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dist(min, max);
        return dist(rng);
    }

    namespace {
        /** The record a soldier_template names. A form id as text ("0x0001FC5B", "MyMod.esp|0x000801") is taken
         *  as it stands; a name goes through the lookup every other spawn uses (Papyrus::LookupLeveledActor). */
        RE::TESBoundObject* ResolveSoldierTemplate(const std::string& templateId) {
            if (templateId.empty()) return nullptr;
            SoldierTemplates::Ref ref;
            if (!SoldierTemplates::ParseRef(templateId, ref)) {
                return Papyrus::LookupLeveledActor(templateId.c_str());
            }
            RE::TESForm* form = nullptr;
            if (ref.plugin.empty()) {
                form = RE::TESForm::LookupByID(ref.formId);
            } else if (auto* data = RE::TESDataHandler::GetSingleton()) {
                form = data->LookupForm(ref.formId, ref.plugin);
            }
            if (!form) return nullptr;
            const auto type = form->GetFormType();
            if (type != RE::FormType::LeveledNPC && type != RE::FormType::NPC) {
                logger::warn("soldier_template '{}' is neither an NPC nor a leveled NPC list (form type {})",
                             templateId, static_cast<int>(type));
                return nullptr;
            }
            return static_cast<RE::TESBoundObject*>(form);
        }

        /** The concrete NPCs a template yields for `count` soldiers at the player's level: a leveled list is
         *  rolled, nested lists too; a plain NPC is taken `count` times. Empty when the list yields nobody,
         *  which is what a list emptied by an overhaul does: such a template counts as not there. */
        std::vector<RE::TESBoundObject*> RollSoldiers(RE::TESBoundObject* base, int count, RE::Actor* player) {
            std::vector<RE::TESBoundObject*> resolved;
            if (!base || count <= 0) return resolved;
            if (base->GetFormType() != RE::FormType::LeveledNPC) {
                resolved.assign(static_cast<std::size_t>(count), base);
                return resolved;
            }
            const auto playerLevel = player ? player->GetLevel() : static_cast<std::uint16_t>(25);
            RE::BSScrapArray<RE::CALCED_OBJECT> calced;
            static_cast<RE::TESLevCharacter*>(base)->CalculateCurrentFormList(
                playerLevel, static_cast<std::int16_t>(count), calced, 0, false);
            for (std::uint32_t ci = 0; ci < calced.size(); ++ci) {
                auto* f = calced[ci].form;
                if (!f) continue;
                for (int d = 0; d < 10 && f->GetFormType() == RE::FormType::LeveledNPC; ++d) {
                    RE::BSScrapArray<RE::CALCED_OBJECT> sub;
                    static_cast<RE::TESLevCharacter*>(f)->CalculateCurrentFormList(playerLevel, 1, sub, 0, false);
                    if (!sub.empty() && sub[0].form) f = sub[0].form;
                    else break;
                }
                if (f->GetFormType() != RE::FormType::LeveledNPC)
                    resolved.push_back(static_cast<RE::TESBoundObject*>(f));
            }
            return resolved;
        }

        /** A spawned soldier loses the factions they came with: the battle side they are put into afterwards
         *  alone decides who is their enemy. With their own factions two groups from the same list stayed
         *  friends (seen 2026-11-14 with both armies from the bandit list: nobody fought), and so would any two
         *  armies whose own factions are friends in the game's data. It also takes them out of every crime faction. */
        void StripOwnFactions(RE::Actor* actor) {
            if (!actor) return;
            std::vector<RE::TESFaction*> own;
            if (auto* base = actor->GetActorBase()) {
                for (const auto& entry : base->factions) {
                    if (entry.faction) own.push_back(entry.faction);
                }
            }
            actor->VisitFactions([&own](RE::TESFaction* faction, std::int8_t) -> bool {
                if (faction) own.push_back(faction);
                return false;
            });
            for (auto* faction : own) actor->AddToFaction(faction, -1);      // -1: not a member, whatever the base says
        }
    }  // namespace

    std::vector<RE::TESBoundObject*> BattleManager::NextOf(bool sideA, const std::string& factionId, int first, int count, int whole,
                                                           RE::Actor* player) {
        std::vector<RE::TESBoundObject*> next;
        bool roll = first <= 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) return next;
            if ((sideA ? activeBattle_->rolledA : activeBattle_->rolledB).empty()) roll = true;
        }
        if (roll) {
            auto rolled = RollFor(factionId, (std::max)(whole, count), player);
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) return next;
            (sideA ? activeBattle_->rolledA : activeBattle_->rolledB) = std::move(rolled);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return next;
        auto& left = sideA ? activeBattle_->rolledA : activeBattle_->rolledB;
        while (count-- > 0 && !left.empty()) {
            next.push_back(left.back());
            left.pop_back();
        }
        return next;
    }

    RE::TESBoundObject* BattleManager::TemplateOf(const std::string& templateId) const {
        {
            std::lock_guard<std::mutex> lock(templatesMutex_);
            if (const auto it = templates_.find(templateId); it != templates_.end()) return it->second;
        }
        auto* found = ResolveSoldierTemplate(templateId);
        std::lock_guard<std::mutex> lock(templatesMutex_);
        templates_[templateId] = found;
        return found;
    }

    std::vector<RE::TESBoundObject*> BattleManager::RollFor(const std::string& factionId, int count, RE::Actor* player) const {
        auto* fp = FactionPolitics::GetSingleton();

        // Whom to raise. The faction's own template first. A faction without a usable one raises its first
        // ally's soldiers that work: the Political Engine realms ship without a template, and a realm allied to
        // the Legion then fields Legion soldiers instead of bandits. The generic bandits come last.
        const auto templateId = fp->GetSoldierTemplate(factionId);
        auto resolved = RollSoldiers(TemplateOf(templateId), count, player);
        if (!resolved.empty()) {
            // A list does not always give as many as it is asked for (the Legion's gave six for eight and three
            // for five, 2026-11-20): it is asked again for the rest.
            for (int again = 0; again < 4 && static_cast<int>(resolved.size()) < count; ++again) {
                const auto more = RollSoldiers(TemplateOf(templateId), count - static_cast<int>(resolved.size()), player);
                resolved.insert(resolved.end(), more.begin(), more.end());
            }
            if (static_cast<int>(resolved.size()) > count) resolved.resize(static_cast<std::size_t>(count));
        } else if (const auto self = fp->GetFaction(factionId)) {
            for (const auto& ally : self->allies) {
                const auto allyTemplate = fp->GetSoldierTemplate(ally);
                resolved = RollSoldiers(TemplateOf(allyTemplate), count, player);
                if (!resolved.empty()) {
                    logger::warn("Battle: template '{}' of {} yields nobody, raised from its ally {}'s '{}'",
                                 templateId, factionId, ally, allyTemplate);
                    break;
                }
            }
        }
        if (resolved.empty()) {
            resolved = RollSoldiers(Papyrus::LookupLeveledActor("LvlBanditMeleeAny"), count, player);
            if (resolved.empty()) {
                if (auto* form = RE::TESForm::LookupByID(SoldierTemplates::kFallbackFormId);
                    form && form->GetFormType() == RE::FormType::LeveledNPC) {
                    resolved = RollSoldiers(static_cast<RE::TESBoundObject*>(form), count, player);
                }
            }
            if (resolved.empty()) {
                logger::error("Battle: template '{}' of {} yields nobody and the bandit fallback failed", templateId, factionId);
            } else {
                logger::warn("Battle: template '{}' of {} yields nobody and no ally has one, using the bandit fallback", templateId, factionId);
            }
        }
        return resolved;
    }

    std::vector<RE::Actor*> BattleManager::RaiseSoldiers(const std::string& factionId, RE::TESFaction* battleFaction,
                                                          const std::vector<Battlefield::Vec>& spots, float facingDeg, float fallbackZ,
                                                          RE::Actor* player, bool standDown, bool commander, bool ofABattle,
                                                          const std::vector<RE::TESBoundObject*>* who, bool champion) {
        std::vector<RE::Actor*> soldiers;
        auto* tes = RE::TES::GetSingleton();
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* world = player ? player->GetWorldspace() : nullptr;
        if (!tes || !data || !world || spots.empty()) return soldiers;

        const auto bases = who ? *who : RollFor(factionId, static_cast<int>(spots.size()), player);
        // Each is made where they are to stand, looking the way their line looks. (3.8 made them all on one spot,
        // the player's when the place of the battle was not at hand, and moved them afterwards.)
        const RE::NiPoint3 rotation{0.f, 0.f, Battlefield::Norm(facingDeg) / kDegPerRad};
        for (std::size_t i = 0; i < bases.size() && i < spots.size(); ++i) {
            RE::NiPoint3 at{spots[i].x, spots[i].y, fallbackZ};
            auto* cell = tes->GetCell(at);
            if (!cell || cell->IsInteriorCell()) continue;          // off the loaded ground: nobody is made there
            float height = 0.f;
            if (tes->GetLandHeight(at, height)) at.z = height + 6.f;
            // The game's own making of a person (their record, what they carry, their mind) is timed by itself: it is
            // most of what a soldier costs, and nothing here can make it less.
            RE::ObjectRefHandle handle;
            {
                static FrameStats made;
                FrameClock clock("Battle: the game making one soldier", made, FrameClock::Now(), 60);
                handle = data->CreateReferenceAtLocation(bases[i], at, rotation, cell, world, nullptr, nullptr, RE::ObjectRefHandle(), true, true);
            }
            const auto ref = handle.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) continue;
            // Switched off until the script lets them fade in, the way the game lets people in the distance appear.
            actor->Disable();

            // At the muster they start nothing; those who come later come to fight. Nobody runs before the army breaks.
            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, standDown ? 0.f : 1.f);
            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kConfidence, 4.f);
            // A soldier of a battle strikes with part of their force and stands more than they would. A champion (the
            // first of a group that brings one) strikes with all of it and stands three times a soldier's measure.
            const bool isChampion = champion && i == 0;
            if (ofABattle) {
                actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAttackDamageMult, isChampion ? Battlefield::kChampionForce : Battlefield::kBlowForce);
                if (const float health = actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth); health > 0.f) {
                    actor->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth,
                                                              health * ((isChampion ? Battlefield::kChampionHardiness : Battlefield::kHardiness) - 1.f));
                }
            }
            StripOwnFactions(actor);
            if (battleFaction) actor->AddToFaction(battleFaction, 0);

            if (ofABattle) RegisterActor(actor, factionId, isChampion ? 3 : (commander && i == 0) ? 2 : 0);
            soldiers.push_back(actor);
        }
        return soldiers;
    }

    // =========================================================================
    // Soldier Query (C++ actor tracking — replaces Papyrus arrays)
    // =========================================================================

    void BattleManager::SetBattleSoldiersAsTeammates(const std::string& side, bool /*isTeammate*/) {
        // kPlayerTeammate is no longer used — it caused cascade attacks.
        // Battle faction membership + StopCombat sweep handles friendly fire.
        // This function remains as a no-op for Papyrus compatibility.
        logger::info("Battle: SetBattleSoldiersAsTeammates called (no-op, faction membership handles it)");
    }

    std::string BattleManager::GetBattleSoldierFormIds(const std::string& side) const {
        std::lock_guard<std::mutex> lock(mutex_);
        nlohmann::json out;
        nlohmann::json formIds = nlohmann::json::array();
        int alive = 0;

        if (activeBattle_) {
            const auto& targetFaction = (IsA(side)) ? activeBattle_->factionA : activeBattle_->factionB;
            for (const auto& actor : activeBattle_->actors) {
                if (actor.factionId == targetFaction) {
                    formIds.push_back(actor.formId);
                    if (actor.alive) alive++;
                }
            }
        }

        out["formIds"] = formIds;
        out["count"] = static_cast<int>(formIds.size());
        out["alive"] = alive;
        return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    int BattleManager::CountDeadSoldiers(const std::string& side) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return 0;

        const auto& targetFaction = (IsA(side)) ? activeBattle_->factionA : activeBattle_->factionB;
        int dead = 0;
        for (const auto& actor : activeBattle_->actors) {
            if (actor.factionId == targetFaction && !actor.alive) dead++;
        }
        return dead;
    }

    // =========================================================================
    // Cleanup (C++ — replaces Papyrus array-based cleanup)
    // =========================================================================

    int BattleManager::CleanupBattleSoldiers(float playerX, float playerY, float playerZ,
                                              float playerAngleZ, bool forceAll) {
        // Read from persistent cleanup list (survives EndBattle/ResetBattleState).
        // Falls back to activeBattle_ if cleanup list is empty (battle still active).
        std::vector<RE::FormID> formIds;
        {
            std::lock_guard<std::mutex> cLock(cleanupMutex_);
            formIds = cleanupFormIds_;
        }
        if (formIds.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                for (const auto& a : activeBattle_->actors) {
                    formIds.push_back(a.formId);
                }
            }
        }

        int remaining = 0;
        float sinA = std::sin(playerAngleZ * 3.14159265f / 180.f);
        float cosA = std::cos(playerAngleZ * 3.14159265f / 180.f);

        for (auto formId : formIds) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(formId);
            if (!actor) continue;  // already deleted or invalid

            if (forceAll) {
                actor->Disable();
                continue;
            }

            if (!actor->Is3DLoaded()) {
                // Unloaded (player changed cells) — safe to clean
                actor->Disable();
                continue;
            }

            float ax = actor->GetPositionX();
            float ay = actor->GetPositionY();
            float dx = ax - playerX;
            float dy = ay - playerY;
            float dist = std::sqrt(dx * dx + dy * dy);

            float threshold = actor->IsDead() ? 2000.f : 2500.f;
            if (dist > threshold) {
                // Check if actor is behind player (heading angle > 90 degrees)
                float dotForward = dx * sinA + dy * cosA;
                if (dotForward < 0.f) {
                    actor->Disable();
                    continue;
                }
            }

            remaining++;
        }

        if (remaining == 0 || forceAll) {
            std::lock_guard<std::mutex> cLock(cleanupMutex_);
            cleanupFormIds_.clear();
        }

        logger::info("[BattleManager] CleanupBattleSoldiers: {} remaining, forceAll={}", remaining, forceAll);
        return remaining;
    }

    void BattleManager::ForgetBattleSoldiers() {
        std::lock_guard<std::mutex> cLock(cleanupMutex_);
        cleanupFormIds_.clear();
    }

    void BattleManager::ForceCleanupAllSoldiers() {
        // Read from persistent cleanup list first, fallback to activeBattle_
        std::vector<RE::FormID> formIds;
        {
            std::lock_guard<std::mutex> cLock(cleanupMutex_);
            formIds = cleanupFormIds_;
            cleanupFormIds_.clear();  // Consumed
        }
        if (formIds.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                for (const auto& a : activeBattle_->actors) {
                    formIds.push_back(a.formId);
                }
            }
        }

        int cleaned = 0;
        for (auto formId : formIds) {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(formId);
            if (actor) {
                actor->Disable();
                cleaned++;
            }
        }
        logger::info("[BattleManager] ForceCleanupAllSoldiers: disabled {} actors", cleaned);
    }

    // =========================================================================
    // Bounty Prevention (C++ — removes crime factions from spawned soldiers)
    // =========================================================================

    void BattleManager::SnapshotBounties() {
        // Nothing to do. A soldier of a battle loses every faction they came with when they are raised (StripOwnFactions),
        // the crime factions among them: killing one is no crime anywhere.
    }

    // ClearBattleBounties and RestoreBounties — REMOVED.
    // Bounty prevention handled at spawn time by removing crime factions from soldiers.

}  // namespace IntelEngine
