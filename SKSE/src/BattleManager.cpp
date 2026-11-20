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
            ground.wet = water > height + 60.f && water < 100000.f;
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
        ba.tier = std::clamp(tier, 0, 2);
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
            activeBattle_->moraleA = std::clamp(activeBattle_->moraleA + delta, 0, 100);
        } else if (factionId == activeBattle_->factionB) {
            activeBattle_->moraleB = std::clamp(activeBattle_->moraleB + delta, 0, 100);
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
        return side == "A" ? activeBattle_->waveA : activeBattle_->waveB;
    }

    void BattleManager::AdvanceWaveOf(const std::string& side) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;
        const bool isA = side == "A";
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

                int tierPenalty = (actor.tier == 2) ? MORALE_PENALTY_LEADER :
                                 (actor.tier == 1) ? MORALE_PENALTY_RECRUITED : MORALE_PENALTY_GENERIC;
                if (isA) penaltyA += tierPenalty;
                else penaltyB += tierPenalty;

                // Track player kills for end-of-battle standing adjustment
                auto* deadActor = RE::TESForm::LookupByID<RE::Actor>(actor.formId);
                if (deadActor) {
                    auto* killer = deadActor->GetKiller();
                    if (killer && killer->IsPlayerRef()) {
                        if (isA) battle.playerKillsA++;
                        else battle.playerKillsB++;
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
        if (penaltyA > 0) battle.moraleA = std::clamp(battle.moraleA - penaltyA, 0, 100);
        if (penaltyB > 0) battle.moraleB = std::clamp(battle.moraleB - penaltyB, 0, 100);

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
            if (battle.mustering || waveStartAlive == 0 || wave >= Battlefield::kWaves) return false;
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
        bool allDeadEndsA = (aliveA == 0 && battle.waveA >= Battlefield::kWaves && !playerOnA);
        bool allDeadEndsB = (aliveB == 0 && battle.waveB >= Battlefield::kWaves && !playerOnB);

        // While the lines are only drawn up nothing is decided. After that an army is beaten when its heart is
        // gone or its last man is. Both in the same moment: whoever has the better of it, or nobody (the
        // second-named always won that one).
        const bool brokenA = !battle.mustering && (battle.moraleA <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsA);
        const bool brokenB = !battle.mustering && (battle.moraleB <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsB);
        if (brokenA && brokenB) {
            battleOver = true;
            const int winner = Battlefield::Verdict(aliveA, aliveB, battle.moraleA, battle.moraleB);
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
        if (!battleOver && !battle.mustering && battle.waveA >= Battlefield::kWaves && battle.waveB >= Battlefield::kWaves) {
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
        if (battleOver) {
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

    void BattleManager::SuppressBountyTick() {
        // (3.8 also set "every hold's" bounty to nothing on every poll of a battle the player had joined. Of the nine
        // ids it used one was a hold's crime faction, Haafingar: whoever owed Solitude gold lost the debt by joining a
        // battle anywhere. The soldiers of a battle belong to no crime faction, so killing them was never a crime.)
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

        if (!factionId.empty()) {
            activeBattle_->playerParticipated = true;

            // Morale boost for allied side, penalty for enemy
            if (factionId == activeBattle_->factionA) {
                activeBattle_->moraleA = std::clamp(activeBattle_->moraleA + MORALE_PLAYER_JOIN_BOOST, 0, 100);
                activeBattle_->moraleB = std::clamp(activeBattle_->moraleB - MORALE_PLAYER_JOIN_ENEMY_PENALTY, 0, 100);
            } else {
                activeBattle_->moraleB = std::clamp(activeBattle_->moraleB + MORALE_PLAYER_JOIN_BOOST, 0, 100);
                activeBattle_->moraleA = std::clamp(activeBattle_->moraleA - MORALE_PLAYER_JOIN_ENEMY_PENALTY, 0, 100);
            }

            logger::info("[BattleManager] Player joined side: {}", factionId);
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
        std::unordered_set<std::string> turnedDown;
        nlohmann::json physicalPosition = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || activeBattle_->id != battleId) {
                out["error"] = "no matching active battle";
                return out.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            }
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

        std::string narrative = drawn
            ? CapThe(nameA) + " and " + The(nameB) + " fought each other to a standstill at " + locationName + ", and both drew off"
            : CapThe(victorName) + " defeated " + The(loserName) + " at " + locationName;
        if (fought) {
            narrative += ". " + playerDesc + " was seen fighting for " + The(fp->GetFactionDisplayName(playerSideWas));
            if (enemyKills > 0) narrative += " and reportedly killed " + std::to_string(enemyKills) + " of the other side";
            if (playerLeft) narrative += ", but left the field before it was decided";
        } else if (playerKillsA + playerKillsB > 0) {
            narrative += ". " + playerDesc + ", of neither side, was seen cutting down soldiers there";
        }

        // --- Record in political DB ---
        // Check for active war first. If war exists, record via RecordOffScreenBattle (updates war state).
        // If no war, record a single political event. Never both — prevents duplicate notifications.
        int warId = fp->GetActiveWarId(factionA, factionB);
        if (warId > 0) {
            fp->RecordOffScreenBattle(factionA, factionB, locationName, drawn ? "draw" : result, narrative, deadA, deadB, drawn ? std::string() : victor, &physicalPosition);
        } else {
            // Single combined event with both factions — adjusts inter-faction relation via delta
            int relationDelta = -10;  // battles worsen relations between the two factions
            fp->RecordPoliticalEvent(factionA, factionB, "battle_result", narrative, relationDelta, gameTime, locationName, physicalPosition);
            logger::info("Battle: No active war — recorded battle_result event (relation {} between {} and {})",
                relationDelta, factionA, factionB);
        }

        // Record player participation standing as a separate single-faction event
        if (fought) {
            int allyChange = playerLeft ? DESERTION_PENALTY : (playerWon ? VICTORY_ALLY_BONUS : DEFEAT_ALLY_BONUS);
            std::string allyName = fp->GetFactionDisplayName(playerSideWas);
            std::string participationDesc = playerDesc + (playerLeft ? " took a place in the line of " : " fought for ") + The(allyName) +
                " at " + locationName + (playerLeft ? " and left it before the end" : (playerWon ? " (victory)" : (drawn ? " (no victor)" : " (defeat)")));
            fp->RecordPoliticalEvent(playerSideWas, "", "player_battle", participationDesc, allyChange, gameTime, locationName, physicalPosition);
        }

        // Record friendly fire as a SEPARATE event (only if it happened)
        if (friendlyKills > 0) {
            int penalty = friendlyKills * KILL_STANDING_PENALTY_PER_SOLDIER;
            std::string killDesc = playerDesc + " reportedly attacked " + std::to_string(friendlyKills) +
                " allied " + fp->GetFactionDisplayName(playerSideWas) + " soldiers during the battle at " + locationName;
            fp->RecordPoliticalEvent(playerSideWas, "", "friendly_fire", killDesc, penalty, gameTime, locationName, physicalPosition);
        }

        // --- What those who saw it remember ---
        std::string fact = "witnessed a battle between " + The(nameA) + " and " + The(nameB) + " at " + locationName + ". " +
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
        // What the winning side pays one who fought in its line (handed over by one of them, in person).
        out["reward"] = Battlefield::Pay(playerWon, enemyKills);

        // What the player reads when it is over. (Nothing, when they are not there to see it.)
        if (playerLeft) {
            out["notification"] = "";
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

        logger::info("Battle: Finalized — result={}, victor={}, playerSide={}, killsA={}, killsB={}, left={}, pay={}",
            result, victor, playerSideWas, playerKillsA, playerKillsB, playerLeft, Battlefield::Pay(playerWon, enemyKills));
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
        const bool isA = side == "A";
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
        const int total = Battlefield::WaveSize(1);
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
        const auto soldiers = RaiseSoldiers(factionId, isA ? battleFactionA : battleFactionB, spots, line.facing, z, player, true, first == 0);
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

    std::string BattleManager::ReinforceLine(const std::string& side, int first, int count, RE::Actor* player) {
        static FrameStats stats;
        FrameClock clock("Battle: bringing up soldiers", stats, FrameClock::Now(), 60);
        nlohmann::json out;
        out["success"] = false;
        out["count"] = 0;
        out["formIds"] = nlohmann::json::array();
        const bool isA = side == "A";
        std::string factionId;
        Battlefield::Vec centre, point;
        float axis = 0.f, z = 0.f;
        int have = 0;
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
            point = isA ? Battlefield::Vec{activeBattle_->reserveAX, activeBattle_->reserveAY}
                        : Battlefield::Vec{activeBattle_->reserveBX, activeBattle_->reserveBY};
            for (const auto& a : activeBattle_->actors) {
                if (a.factionId == factionId) ++have;
            }
        }
        count = (std::min)(count, MAX_SOLDIERS_PER_SIDE - have);
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
            for (const auto& at : Battlefield::ReservePoints(line, me, Battlefield::Norm(player->GetAngleZ() * kDegPerRad))) {
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
            logger::info("Battle: more of {} come up behind their line, {:.0f} units from the player", factionId, Battlefield::Dist(me, point));
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                (isA ? activeBattle_->reserveAX : activeBattle_->reserveBX) = point.x;
                (isA ? activeBattle_->reserveAY : activeBattle_->reserveBY) = point.y;
            }
        }
        std::vector<Battlefield::Vec> spots;
        for (int i = 0; i < count; ++i) spots.push_back(Battlefield::ReserveSpot(point, line.facing, (std::max)(first, 0) + i));

        const auto soldiers = RaiseSoldiers(factionId, isA ? battleFactionA : battleFactionB, spots, line.facing, z, player, false, false);
        nlohmann::json ids = nlohmann::json::array();
        for (auto* s : soldiers) ids.push_back(s->GetFormID());
        out["count"] = static_cast<int>(soldiers.size());
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

    void BattleManager::BeginFight() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_ || !activeBattle_->mustering) return;
        activeBattle_->mustering = false;
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

    std::string BattleManager::Say(const std::string& what, const std::string& a, const std::string& b, const std::string& name, int number,
                                   int other) const {
        auto* fp = FactionPolitics::GetSingleton();
        std::string place;
        bool fighting = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                place = activeBattle_->locationName;
                fighting = !activeBattle_->mustering;
            }
        }
        if (place.empty()) place = "here";
        const std::string theA = a.empty() ? std::string() : The(fp->GetFactionDisplayName(a));
        const std::string theB = b.empty() ? std::string() : The(fp->GetFactionDisplayName(b));

        // ---- what the player reads
        if (what == "drawn_up") return "Two armies are drawn up near " + place + ": " + theA + " and " + theB + ".";
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

        // ---- what somebody is given to say, in their own words (told to the model; a: their side, b: the other)
        if (what == "hail") {
            // number: 1 when their side counts this person a friend.
            return "leads the soldiers of " + theA +
                   (fighting ? ", in the thick of a battle against " + theB + " near " + place
                             : ", drawn up for battle against " + theB + " near " + place + ", moments before the lines meet") +
                   ", sees " + name + " close by (" +
                   (number > 0 ? "whom they know as a friend of their side" : "a stranger to them, but another blade is welcome") +
                   ") and calls out to them, in a few words and as a soldier would: will they stand with " + theA +
                   " today, or keep out of the way?";
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
            line += " " + name + " fights in your line.";
        } else if (playerSide == foe) {
            line += " " + name + " fights for the enemy.";
        } else if (Battlefield::WouldHave(fp->GetPlayerStanding(own))) {
            line += " " + name + " has taken no side. If they say they will fight on yours, they are welcome in your line.";
        } else {
            line += " Your side knows " + name + " for an enemy.";
        }
        return line;
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
                                                          RE::Actor* player, bool standDown, bool commander) {
        std::vector<RE::Actor*> soldiers;
        auto* tes = RE::TES::GetSingleton();
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* world = player ? player->GetWorldspace() : nullptr;
        if (!tes || !data || !world || spots.empty()) return soldiers;

        const auto bases = RollFor(factionId, static_cast<int>(spots.size()), player);
        // Each is made where they are to stand, looking the way their line looks. (3.8 made them all on one spot,
        // the player's when the place of the battle was not at hand, and moved them afterwards.)
        const RE::NiPoint3 rotation{0.f, 0.f, Battlefield::Norm(facingDeg) / kDegPerRad};
        for (std::size_t i = 0; i < bases.size() && i < spots.size(); ++i) {
            RE::NiPoint3 at{spots[i].x, spots[i].y, fallbackZ};
            auto* cell = tes->GetCell(at);
            if (!cell || cell->IsInteriorCell()) continue;          // off the loaded ground: nobody is made there
            float height = 0.f;
            if (tes->GetLandHeight(at, height)) at.z = height + 6.f;
            const auto handle = data->CreateReferenceAtLocation(bases[i], at, rotation, cell, world, nullptr, nullptr, RE::ObjectRefHandle(), true, true);
            const auto ref = handle.get();
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) continue;
            // Switched off until the script lets them fade in, the way the game lets people in the distance appear.
            actor->Disable();

            // At the muster they start nothing; those who come later come to fight. Nobody runs before the army breaks.
            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, standDown ? 0.f : 1.f);
            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kConfidence, 4.f);
            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAttackDamageMult, Battlefield::kBlowForce);
            if (const float health = actor->AsActorValueOwner()->GetPermanentActorValue(RE::ActorValue::kHealth); health > 0.f) {
                actor->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kHealth,
                                                          health * (Battlefield::kHardiness - 1.f));
            }
            StripOwnFactions(actor);
            if (battleFaction) actor->AddToFaction(battleFaction, 0);

            RegisterActor(actor, factionId, (commander && i == 0) ? 2 : 0);
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
            const auto& targetFaction = (side == "A") ? activeBattle_->factionA : activeBattle_->factionB;
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

        const auto& targetFaction = (side == "A") ? activeBattle_->factionA : activeBattle_->factionB;
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
