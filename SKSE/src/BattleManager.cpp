/**
 * BattleManager Implementation
 *
 * Tracks one active battle at a time. Papyrus drives spawning and cleanup;
 * C++ tracks actor state, morale, and generates poll events.
 */

#include "BattleManager.h"
#include "LocationResolver.h"
#include "FactionPolitics.h"

#include <algorithm>
#include <cmath>

namespace IntelEngine {

    // Named constants for battle tuning
    constexpr int MORALE_PENALTY_GENERIC = 3;
    constexpr int MORALE_PENALTY_RECRUITED = 5;
    constexpr int MORALE_PENALTY_LEADER = 30;
    constexpr int MORALE_BATTLE_END_THRESHOLD = 20;
    constexpr float WAVE_CASUALTY_THRESHOLD = 0.4f;
    constexpr int MORALE_PLAYER_JOIN_BOOST = 15;
    constexpr int MORALE_PLAYER_JOIN_ENEMY_PENALTY = 5;

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

        logger::info("[BattleManager] Battle {} ended: result={}, victor={}",
                     battleId, result, victor);
        activeBattle_.reset();
    }

    bool BattleManager::IsBattleActive() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return activeBattle_.has_value();
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

    void BattleManager::AdvanceWave() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!activeBattle_) return;
        activeBattle_->currentWave++;

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
        auto shouldSpawnWave = [](int waveStartAlive, int alive) -> bool {
            if (waveStartAlive == 0) return false;
            float casualtyRate = 1.0f - (static_cast<float>(alive) / static_cast<float>(waveStartAlive));
            return casualtyRate >= WAVE_CASUALTY_THRESHOLD;
        };

        // Battle over conditions
        bool battleOver = false;
        std::string battleResult;
        std::string battleVictor;

        // Only count aliveX==0 as defeat after reserves committed (wave 3).
        // Before that, more soldiers haven't spawned yet — don't end prematurely.
        bool allDeadEndsA = (aliveA == 0 && battle.currentWave >= 3);
        bool allDeadEndsB = (aliveB == 0 && battle.currentWave >= 3);

        if (battle.moraleA <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsA) {
            battleOver = true;
            battleResult = "defender_victory";
            battleVictor = battle.factionB;
        } else if (battle.moraleB <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsB) {
            battleOver = true;
            battleResult = "attacker_victory";
            battleVictor = battle.factionA;
        }

        // Build result JSON
        result["events"] = events;
        result["morale_a"] = battle.moraleA;
        result["morale_b"] = battle.moraleB;
        result["alive_a"] = aliveA;
        result["alive_b"] = aliveB;
        result["wave"] = battle.currentWave;
        // Serialize booleans as strings — Papyrus StoryResponseGetField extracts string values
        result["should_spawn_wave_a"] = shouldSpawnWave(battle.waveStartAliveA, aliveA) ? "true" : "false";
        result["should_spawn_wave_b"] = shouldSpawnWave(battle.waveStartAliveB, aliveB) ? "true" : "false";
        result["battle_over"] = battleOver ? "true" : "false";
        if (battleOver) {
            result["result"] = battleResult;
            result["victor"] = battleVictor;
            result["player_kills_a"] = battle.playerKillsA;
            result["player_kills_b"] = battle.playerKillsB;
        }

        // Include player side in output
        result["player_side"] = battle.playerSide;
        result["player_participated"] = battle.playerParticipated ? "true" : "false";

        return result.dump();
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

        auto* markerRef = bgsLocation->worldLocMarker.get().get();
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

        pendingBattles_.push_back(std::move(pb));

        logger::info("[BattleManager] Pending battle {} added: {} vs {} at {} ({:.0f}, {:.0f}, {:.0f}), deadline {:.4f}",
                     pendingBattles_.back().id, factionA, factionB, locationName, pos.x, pos.y, pos.z, pendingBattles_.back().deadline);

        return pendingBattles_.back().id;
    }

    int BattleManager::PollPendingBattles(float playerX, float playerY, float playerZ) {
        auto* cal = RE::Calendar::GetSingleton();
        float now = cal ? cal->GetCurrentGameTime() : 0.0f;

        // Collect expired battles under lock, then process outside lock to avoid
        // holding pendingMutex_ while calling RecordOffScreenBattle (which acquires DB mutex).
        // This prevents lock-order inversion deadlocks.
        struct ExpiredBattle {
            std::string factionA, factionB, locationName, resultJson;
        };
        std::vector<ExpiredBattle> expired;
        int triggeredId = -1;

        {
            std::lock_guard<std::mutex> lock(pendingMutex_);

            // First pass: collect expired battles
            for (auto it = pendingBattles_.begin(); it != pendingBattles_.end(); ) {
                if (now >= it->deadline) {
                    logger::info("[BattleManager] Pending battle {} expired ({}): {} vs {} — recording off-screen",
                                 it->id, it->locationName, it->factionA, it->factionB);
                    expired.push_back({it->factionA, it->factionB, it->locationName, it->resultJson});
                    it = pendingBattles_.erase(it);
                } else {
                    ++it;
                }
            }

            // Second pass: check player proximity to remaining battles
            for (auto& pb : pendingBattles_) {
                float dx = playerX - pb.x;
                float dy = playerY - pb.y;
                float dist = std::sqrt(dx * dx + dy * dy);  // 2D distance (Z irrelevant for overworld)

                if (dist <= PENDING_TRIGGER_DISTANCE) {
                    logger::info("[BattleManager] Pending battle {} triggered! Player within {:.0f} units of {} ({:.0f}, {:.0f})",
                                 pb.id, dist, pb.locationName, pb.x, pb.y);
                    triggeredId = pb.id;
                    break;
                }
            }
        }  // pendingMutex_ released

        // Process expired battles outside the lock
        for (const auto& eb : expired) {
            try {
                auto j = nlohmann::json::parse(eb.resultJson);
                std::string battleResult = j.value("battle_result", "draw");
                std::string victor = j.value("battle_victor", "");
                int lossesA = j.value("attacker_losses", 0);
                int lossesB = j.value("defender_losses", 0);
                std::string narrative = j.value("description", "Battle resolved without witnesses.");

                FactionPolitics::GetSingleton()->RecordOffScreenBattle(
                    eb.factionA, eb.factionB, eb.locationName,
                    battleResult, narrative, lossesA, lossesB, victor);

                // Queue expired result for Papyrus RESULT notification
                nlohmann::json expiredInfo;
                expiredInfo["faction_a"] = eb.factionA;
                expiredInfo["faction_b"] = eb.factionB;
                expiredInfo["location"] = eb.locationName;
                expiredInfo["victor"] = victor;
                expiredInfo["result"] = battleResult;
                {
                    std::lock_guard<std::mutex> lock(pendingMutex_);
                    expiredResults_.push_back(expiredInfo.dump());
                }
            } catch (const std::exception& e) {
                logger::error("[BattleManager] Failed to parse expired battle JSON: {}", e.what());
            }
        }

        return triggeredId;
    }

    void BattleManager::ClearPendingBattles() {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingBattles_.clear();
        expiredResults_.clear();
        logger::info("[BattleManager] Cleared all pending battles and expired results (game reload)");
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
                j["result_json"] = pb.resultJson;
                return j.dump();
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

}  // namespace IntelEngine
