/**
 * BattleManager Implementation
 *
 * Tracks one active battle at a time. Papyrus drives spawning and cleanup;
 * C++ tracks actor state, morale, and generates poll events.
 */

#include "BattleManager.h"
#include "LocationResolver.h"
#include "FactionPolitics.h"
#include "StringUtils.h"
#include "ProcessUtils.h"

#include <algorithm>
#include <cmath>

namespace IntelEngine {

    // Named constants for battle tuning
    constexpr int MORALE_PENALTY_GENERIC = 2;
    constexpr int MORALE_PENALTY_RECRUITED = 3;
    constexpr int MORALE_PENALTY_LEADER = 15;
    constexpr int MORALE_BATTLE_END_THRESHOLD = 15;
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
        // If the PLAYER is on side A and still alive, side A isn't truly defeated —
        // the player can solo the remaining enemies. Same logic for side B.
        bool playerOnA = (battle.playerSide == battle.factionA);
        bool playerOnB = (battle.playerSide == battle.factionB);
        bool allDeadEndsA = (aliveA == 0 && battle.currentWave >= 3 && !playerOnA);
        bool allDeadEndsB = (aliveB == 0 && battle.currentWave >= 3 && !playerOnB);

        if (battle.moraleA <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsA) {
            battleOver = true;
            battleResult = battle.factionB + "_victory";
            battleVictor = battle.factionB;
        } else if (battle.moraleB <= MORALE_BATTLE_END_THRESHOLD || allDeadEndsB) {
            battleOver = true;
            battleResult = battle.factionA + "_victory";
            battleVictor = battle.factionA;
        }

        // If all ENEMY soldiers are dead but player's side still has the player alive,
        // that's a victory for the player's side (player soloed the rest)
        if (!battleOver && battle.currentWave >= 3) {
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

        // Continuous bounty suppression — clear bounty and stop hostile guards every poll.
        // Crime faction removal doesn't prevent bounty (engine uses cell crime faction, not membership).
        if (!battle.playerSide.empty()) {
            suppressBattleBounty_ = true;
        }

        return result.dump();
    }

    void BattleManager::SuppressBountyTick() {
        if (!suppressBattleBounty_) return;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        // Clear bounty from all holds
        for (auto fid : kCrimeFactionIds) {
            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(fid);
            if (faction && (faction->GetCrimeGold() > 0)) {
                faction->SetCrimeGold(0);
                faction->SetCrimeGoldViolent(0);
            }
        }

        // Check if player has positive standing with the allied faction.
        // If yes, wipe assault memory from any hostile allied NPC.
        // SetBeenAttacked(false) clears the ROOT CAUSE — unlike StopCombat(),
        // the actor won't re-engage because it no longer remembers being hit.
        // This is effectively a one-shot fix per actor (not continuous polling).
        std::string playerSide;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || activeBattle_->playerSide.empty()) return;
            playerSide = activeBattle_->playerSide;
        }

        auto* fp = FactionPolitics::GetSingleton();
        int standing = fp->GetPlayerStanding(playerSide);
        if (standing < 30) return;  // only forgive friendly fire for allied factions (30+ standing)

        // Wipe assault memory from any NPC hostile to the player who belongs to
        // the allied political faction. Covers spawned soldiers, enlisted guards,
        // AND unenlisted guards/NPCs that witnessed the assault.
        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) -> bool {
            if (!actor || actor == player || actor->IsDead()) return false;
            if (!actor->IsHostileToActor(player)) return false;

            // Only forgive allies — enemy soldiers SHOULD attack the player
            auto npcFaction = fp->GetNPCFactionId(actor);
            if (npcFaction != playerSide) return false;

            // Clear assault memory (the actual fix — StopCombat alone doesn't work)
            actor->SetBeenAttacked(false);
            actor->GetActorRuntimeData().boolFlags.reset(RE::Actor::BOOL_FLAGS::kAngryWithPlayer);
            actor->StopCombat();
            logger::info("Battle: Cleared assault memory on {} ({:08X})",
                actor->GetDisplayFullName(), actor->GetFormID());
            return false;
        });
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
        if (!activeBattle_ || activeBattle_->sideAFaction.empty()) return "A";
        return (factionId == activeBattle_->sideAFaction) ? "A" : "B";
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

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_ || activeBattle_->id != battleId) {
                out["error"] = "no matching active battle";
                return out.dump();
            }
            playerSideWas = activeBattle_->playerSide;
            factionA = activeBattle_->factionA;
            factionB = activeBattle_->factionB;
            playerKillsA = activeBattle_->playerKillsA;
            playerKillsB = activeBattle_->playerKillsB;
        }

        auto* fp = FactionPolitics::GetSingleton();
        std::string victorName = fp->GetFactionDisplayName(victor);
        std::string loser = (victor == factionA) ? factionB : factionA;
        std::string loserName = fp->GetFactionDisplayName(loser);

        // --- Apply kill-based standing ---
        // Enemy kills: expected in battle, no penalty. Friendly fire: standing penalty only (no bounty).
        // Player was on playerSideWas. Kills of that faction = friendly fire. Kills of enemy = free.
        int friendlyKills = 0;
        std::string friendlyFaction;
        if (!playerSideWas.empty()) {
            if (playerSideWas == factionA) {
                friendlyKills = playerKillsA;  // killed own faction
                friendlyFaction = factionA;
            } else {
                friendlyKills = playerKillsB;  // killed own faction
                friendlyFaction = factionB;
            }
        }
        if (friendlyKills > 0) {
            int penalty = friendlyKills * KILL_STANDING_PENALTY_PER_SOLDIER;
            fp->AdjustPlayerStanding(friendlyFaction, penalty);
            logger::info("Battle: Friendly fire — player killed {} allied {} soldiers ({} standing)",
                friendlyKills, friendlyFaction, penalty);
        }
        // Enemy kills: no standing penalty (they're the enemy, killing them is the point)

        // --- Apply post-battle standing ---
        bool spectatorPenalized = false;
        bool playerWon = (playerSideWas == victor);
        std::string enemyFaction = (playerSideWas == factionA) ? factionB : factionA;

        if (!playerSideWas.empty() && !victor.empty()) {
            // Player fought
            if (playerWon) {
                fp->AdjustPlayerStanding(playerSideWas, VICTORY_ALLY_BONUS);
                fp->AdjustPlayerStanding(enemyFaction, VICTORY_ENEMY_PENALTY);
            } else {
                fp->AdjustPlayerStanding(playerSideWas, DEFEAT_ALLY_BONUS);
                fp->AdjustPlayerStanding(enemyFaction, DEFEAT_ENEMY_PENALTY);
            }
        } else if (!victor.empty()) {
            // Spectator consequences
            int standingA = fp->GetPlayerStanding(factionA);
            int standingB = fp->GetPlayerStanding(factionB);
            // Skip if high standing with both (neutral is valid)
            if (!(standingA >= AUTO_JOIN_STANDING_THRESHOLD && standingB >= AUTO_JOIN_STANDING_THRESHOLD)) {
                if (standingA >= SPECTATOR_PENALTY_THRESHOLD) {
                    fp->AdjustPlayerStanding(factionA, SPECTATOR_PENALTY);
                    spectatorPenalized = true;
                }
                if (standingB >= SPECTATOR_PENALTY_THRESHOLD) {
                    fp->AdjustPlayerStanding(factionB, SPECTATOR_PENALTY);
                    spectatorPenalized = true;
                }
            }
        }

        // Write updated standings
        fp->WritePoliticalStateFile();

        // --- Build narrative ---
        auto* player = RE::PlayerCharacter::GetSingleton();
        std::string playerName = player ? player->GetDisplayFullName() : "the Dragonborn";
        std::string playerDesc = "someone matching the description of " + playerName;

        std::string narrative = victorName + " defeated " + loserName + " at " + locationName;
        if (!playerSideWas.empty()) {
            narrative += ". " + playerDesc + " was seen fighting for " + fp->GetFactionDisplayName(playerSideWas);
            int totalKills = playerKillsA + playerKillsB;
            if (totalKills > 0) {
                narrative += " and reportedly killed " + std::to_string(totalKills) + " soldiers";
            }
        }

        // --- Record in political DB ---
        // Check for active war first. If war exists, record via RecordOffScreenBattle (updates war state).
        // If no war, record a single political event. Never both — prevents duplicate notifications.
        int warId = fp->GetActiveWarId(factionA, factionB);
        if (warId > 0) {
            fp->RecordOffScreenBattle(factionA, factionB, locationName, result, narrative, deadA, deadB, victor);
        } else {
            // Single combined event with both factions — adjusts inter-faction relation via delta
            int relationDelta = -10;  // battles worsen relations between the two factions
            fp->RecordPoliticalEvent(factionA, factionB, "battle_result", narrative, relationDelta, gameTime);
            logger::info("Battle: No active war — recorded battle_result event (relation {} between {} and {})",
                relationDelta, factionA, factionB);
        }

        // Record player participation standing as a separate single-faction event
        if (!playerSideWas.empty()) {
            int allyChange = playerWon ? VICTORY_ALLY_BONUS : DEFEAT_ALLY_BONUS;
            std::string allyName = fp->GetFactionDisplayName(playerSideWas);
            std::string participationDesc = playerDesc + " fought for the " + allyName +
                " at " + locationName + " (" + (playerWon ? "victory" : "defeat") + ")";
            fp->RecordPoliticalEvent(playerSideWas, "", "player_battle", participationDesc, allyChange, gameTime);
        }

        // Record friendly fire as a SEPARATE event (only if it happened)
        if (friendlyKills > 0) {
            int penalty = friendlyKills * KILL_STANDING_PENALTY_PER_SOLDIER;
            std::string killDesc = playerDesc + " reportedly attacked " + std::to_string(friendlyKills) +
                " allied " + fp->GetFactionDisplayName(friendlyFaction) + " soldiers during the battle at " + locationName;
            fp->RecordPoliticalEvent(friendlyFaction, "", "friendly_fire", killDesc, penalty, gameTime);
        }

        // --- Inject witness memories ---
        // Build fact text
        std::string fact = "witnessed a battle between " + victorName + " and " + loserName +
            " forces at " + locationName + ". " + victorName + " prevailed";
        int totalCasualties = deadA + deadB;
        if (totalCasualties > 0) {
            fact += " with " + std::to_string(totalCasualties) + " casualties";
        }
        if (!playerSideWas.empty()) {
            std::string allySideName = fp->GetFactionDisplayName(playerSideWas);
            if (playerWon) {
                fact += ". someone resembling " + playerName + " was seen fighting for " + allySideName;
            } else {
                fact += ". someone resembling " + playerName + " was the last one standing for " +
                    allySideName + ", fighting alone after all allied soldiers fell";
            }
            int totalKills = playerKillsA + playerKillsB;
            if (totalKills > 0) {
                fact += " and appeared to have killed " + std::to_string(totalKills) + " enemy soldiers";
            }
        }
        // Store fact for Papyrus to inject (C++ can't call SkyrimNet InjectFact directly)
        out["witnessFact"] = fact;

        // --- End battle in C++ ---
        EndBattle(battleId, result, victor);

        // --- Build return JSON ---
        out["playerSideWas"] = playerSideWas;
        out["playerWon"] = playerWon;
        out["victorName"] = victorName;
        out["loserName"] = loserName;
        out["spectatorPenalized"] = spectatorPenalized;
        out["factionA"] = factionA;
        out["factionB"] = factionB;
        out["playerKillsA"] = playerKillsA;
        out["playerKillsB"] = playerKillsB;

        // Notification text
        if (!playerSideWas.empty()) {
            if (playerWon) {
                out["notification"] = "The " + victorName + " banner stands over " + locationName + ". The battle is yours.";
            } else {
                std::string allyName = (playerSideWas == factionA) ? fp->GetFactionDisplayName(factionA)
                                                                    : fp->GetFactionDisplayName(factionB);
                out["notification"] = "The last of the " + allyName + " fell around you. You stood alone against the " +
                    victorName + " — the field at " + locationName + " is lost.";
            }
        } else {
            out["notification"] = "The fighting ends. " + victorName + " banners now fly over " + locationName + ".";
            if (spectatorPenalized) {
                out["spectatorNotification"] = "Your inaction has not gone unnoticed.";
            }
        }

        // Do NOT restore player crime factions here — soldiers are still alive during
        // deferred cleanup. If the player swings at a remaining enemy near a guard,
        // restored crime factions would generate bounty. Crime factions are restored
        // by Papyrus RemovePlayerFromBattle when all soldiers are disabled.
        // playerCrimeFactionsRemoved_ stays true so CleanupStaleBattleState can
        // restore them on game load if cleanup didn't finish.

        // Restore enlisted guards (remove battle faction, restore crime factions, stop combat).
        // MUST happen after EndBattle so guards don't try to re-engage battle targets.
        RestoreEnlistedGuards();

        // Don't remove player from battle faction here — Papyrus keeps the player in the
        // faction until deferred cleanup finishes (all soldiers disabled). Same-faction
        // membership prevents retaliation from friendly fire. No ForEachLoadedActor sweep needed.

        logger::info("Battle: Finalized — result={}, victor={}, playerSide={}, killsA={}, killsB={}",
            result, victor, playerSideWas, playerKillsA, playerKillsB);
        return out.dump();
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
            return out.dump();
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

        return out.dump();
    }

    std::string BattleManager::EvaluatePlayerJoin(const std::string& questAutoJoinFaction) {
        nlohmann::json out;
        out["shouldJoin"] = false;

        std::string factionA, factionB;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) return out.dump();
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
            return out.dump();
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

        return out.dump();
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
            return out.dump();
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (activeBattle_) {
                out["failReason"] = "battle system busy";
                return out.dump();
            }
        }

        // Check scheduled battles too
        // (BattleScheduled is Papyrus state — caller must check separately)

        out["canStart"] = true;
        out["enemyFaction"] = enemy;
        return out.dump();
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
        return out.dump();
    }

    std::string BattleManager::GetPollAction(const std::string& stateJson) {
        nlohmann::json out;
        out["action"] = "none";

        if (stateJson.empty() || stateJson == "{}") {
            out["action"] = "battle_end_external";
            return out.dump();
        }

        try {
            auto j = nlohmann::json::parse(stateJson);
            if (j.value("battle_over", std::string("false")) == "true") {
                out["action"] = "battle_end";
                out["result"] = j.value("result", "draw");
                out["victor"] = j.value("victor", "");
                return out.dump();
            }

            // Check wave spawn triggers
            if (j.contains("spawn_wave")) {
                out["action"] = "spawn_wave";
                out["waveNum"] = j["spawn_wave"].get<int>();
                return out.dump();
            }
        } catch (const std::exception& e) {
            logger::error("GetPollAction: JSON parse error: {}", e.what());
            out["action"] = "error";
        }

        return out.dump();
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

    void BattleManager::EnlistFriendlyGuards(const std::string& playerFactionId,
                                              RE::TESFaction* battleFaction, RE::Actor* player) {
        // Add nearby friendly guards to the player's battle faction so stray hits
        // are forgiven (same-faction). Only guards within 3000 units — no need to
        // enlist guards across the entire cell.
        // Crime faction removal from guards is unnecessary — the player's own crime
        // factions are already removed, so no bounty accrues regardless.
        auto* fp = FactionPolitics::GetSingleton();
        modifiedGuardFormIds_.clear();
        float px = player->GetPositionX();
        float py = player->GetPositionY();
        int count = 0;
        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) -> bool {
            if (!actor || actor == player || actor->IsDead()) return false;
            auto npcFaction = fp->GetNPCFactionId(actor);
            if (npcFaction != playerFactionId) return false;

            // Proximity filter — only enlist guards near the battle
            float dx = actor->GetPositionX() - px;
            float dy = actor->GetPositionY() - py;
            if ((dx * dx + dy * dy) > 9000000.f) return false;  // 3000^2

            actor->AddToFaction(battleFaction, 0);
            modifiedGuardFormIds_.push_back(actor->GetFormID());
            count++;
            return false;
        });
        if (count > 0) {
            logger::info("Battle: Enlisted {} nearby {} guards into battle faction", count, playerFactionId);
        }
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

        // Restore all enlisted guards (centralized: removes battle faction, restores crime factions)
        RestoreEnlistedGuards();

        // Restore player crime factions if they were removed and never restored
        if (playerCrimeFactionsRemoved_) {
            RestorePlayerCrimeFactions();  // also clears residual bounty
        }
    }

    void BattleManager::RemovePlayerCrimeFactions() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        for (auto fid : kCrimeFactionIds) {
            auto* cf = RE::TESForm::LookupByID<RE::TESFaction>(fid);
            if (cf) player->AddToFaction(cf, -1);
        }
        playerCrimeFactionsRemoved_ = true;
        logger::info("Battle: Player removed from crime factions (quest-level immunity)");
    }

    void BattleManager::RestorePlayerCrimeFactions() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        for (auto fid : kCrimeFactionIds) {
            auto* cf = RE::TESForm::LookupByID<RE::TESFaction>(fid);
            if (cf) player->AddToFaction(cf, 0);
        }
        for (auto fid : kCrimeFactionIds) {
            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(fid);
            if (faction && faction->GetCrimeGold() > 0) {
                faction->SetCrimeGold(0);
                faction->SetCrimeGoldViolent(0);
            }
        }
        playerCrimeFactionsRemoved_ = false;
        logger::info("Battle: Player crime factions restored + bounty cleared");
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
        return out.dump();
    }

    std::string BattleManager::ExecuteFullBattleSpawn(const std::string& questAutoJoinFaction,
                                                       RE::Actor* player, float playerAngleZ,
                                                       RE::TESObjectREFR* spawnAnchor) {
        nlohmann::json out;
        out["success"] = false;

        if (!player) {
            out["error"] = "player is null";
            return out.dump();
        }

        // --- Step 1: Determine factions from active battle ---
        std::string factionA, factionB;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) {
                out["error"] = "no active battle";
                return out.dump();
            }
            factionA = activeBattle_->factionA;
            factionB = activeBattle_->factionB;
        }

        // --- Step 2: Player auto-join (before any soldiers exist) ---
        auto joinResult = EvaluatePlayerJoin(questAutoJoinFaction);
        auto joinJson = nlohmann::json::parse(joinResult, nullptr, false);
        bool playerJoined = false;
        std::string joinFaction;
        std::string joinNotification;
        if (!joinJson.is_discarded() && joinJson.value("shouldJoin", false)) {
            joinFaction = joinJson.value("joinFaction", "");
            auto displayName = joinJson.value("displayName", "");
            bool isQuest = joinJson.value("isQuestJoin", false);
            if (!joinFaction.empty()) {
                SetPlayerSide(joinFaction);
                playerJoined = true;
                joinNotification = isQuest
                    ? "You join the " + displayName + " on the field."
                    : "The " + displayName + " soldiers recognize you as an ally.";
            }
        }

        // --- Step 3: Determine spawn reference ---
        // Prefer spawnAnchor (quest location) — soldiers appear AT the battle site.
        // Fallback to player if anchor is null or not loaded.
        RE::TESObjectREFR* spawnRef = spawnAnchor;
        if (!spawnRef || !spawnRef->Is3DLoaded()) {
            spawnRef = player;
            logger::info("ExecuteFullBattleSpawn: anchor not loaded, spawning at player");
        } else {
            logger::info("ExecuteFullBattleSpawn: spawning at anchor {:08X} ({:.0f}, {:.0f}, {:.0f})",
                spawnRef->GetFormID(), spawnRef->GetPositionX(), spawnRef->GetPositionY(), spawnRef->GetPositionZ());
        }

        // Calculate perpendicular spread direction
        // Direction: from anchor toward player (so battle faces the approaching player)
        float anchorX = spawnRef->GetPositionX();
        float anchorY = spawnRef->GetPositionY();
        float px = player->GetPositionX();
        float py = player->GetPositionY();
        float dx = px - anchorX;
        float dy = py - anchorY;
        float dist = std::sqrt(dx * dx + dy * dy);

        float dirX, dirY;
        if (dist > 10.f) {
            dirX = dx / dist;
            dirY = dy / dist;
        } else {
            // Player is on top of anchor — use player facing
            float rad = playerAngleZ * 3.14159265f / 180.f;
            dirX = std::sin(rad);
            dirY = std::cos(rad);
        }

        // Perpendicular to the player-anchor axis — keep sides close (300 total max)
        // so Skyrim's 512-unit combat detection triggers immediately
        float perpX = -dirY * 100.f;
        float perpY = dirX * 100.f;

        // If spawning at anchor, offset 1500 units TOWARD player (outside town)
        float spawnCenterX = anchorX;
        float spawnCenterY = anchorY;
        if (spawnRef != player && dist > 1500.f) {
            spawnCenterX = anchorX + dirX * 1500.f;
            spawnCenterY = anchorY + dirY * 1500.f;
        } else if (spawnRef != player) {
            // Player is close to anchor — use midpoint
            spawnCenterX = (anchorX + px) / 2.f;
            spawnCenterY = (anchorY + py) / 2.f;
        }

        // --- Step 4: Spawn soldiers (using shared helper) ---
        auto soldiersA = SpawnSoldiersForFaction(factionA, nullptr, WAVE1_SOLDIERS,
            spawnRef, player, perpX, perpY, spawnCenterX, spawnCenterY);
        auto soldiersB = SpawnSoldiersForFaction(factionB, nullptr, WAVE1_SOLDIERS,
            spawnRef, player, -perpX, -perpY, spawnCenterX, spawnCenterY);

        // --- Step 4b: Validate spawn --- both sides must have soldiers
        if (soldiersA.empty() || soldiersB.empty()) {
            logger::error("ExecuteFullBattleSpawn: spawn failed — A={}, B={} (need both > 0)",
                soldiersA.size(), soldiersB.size());
            // Disable any soldiers that did spawn
            for (auto* a : soldiersA) { if (a) a->Disable(); }
            for (auto* b : soldiersB) { if (b) b->Disable(); }
            out["success"] = false;
            out["error"] = "one side failed to spawn";
            return out.dump();
        }

        // --- Step 5: Add soldiers to ESP battle factions ---
        // Normalize: player's allied faction ALWAYS gets Intel_BattleSideA, enemy gets SideB.
        // This prevents the side-swap bug where the DM lists factions in arbitrary order
        // (e.g., "Thalmor vs Stormcloaks" one tick, "Stormcloaks vs Thalmor" the next),
        // causing the player to end up on different ESP factions between consecutive battles.
        auto [battleFactionA, battleFactionB] = ResolveBattleFactions();

        if (battleFactionA && battleFactionB) {
            // Determine which soldiers are allies vs enemies (for faction assignment)
            bool allyIsA = !playerJoined || (joinFaction == factionA);
            auto& allySoldiers = allyIsA ? soldiersA : soldiersB;
            auto& enemySoldiers = allyIsA ? soldiersB : soldiersA;
            const auto& allyFactionId = allyIsA ? factionA : factionB;
            const auto& enemyFactionId = allyIsA ? factionB : factionA;

            // Defensive: clear player from BOTH battle factions before adding to new side.
            // Papyrus RemoveFromFaction may not have propagated if the player was teleported
            // between battles (e.g., arrest → jail → new cell). Without this, the player
            // can end up in both SideA and SideB simultaneously — these factions are enemies,
            // so every NPC attacks the player.
            if (player) {
                player->AddToFaction(battleFactionA, -1);
                player->AddToFaction(battleFactionB, -1);
            }

            // Allies always SideA, enemies always SideB
            for (auto* a : allySoldiers) {
                if (a) a->AddToFaction(battleFactionA, 0);
            }
            for (auto* b : enemySoldiers) {
                if (b) b->AddToFaction(battleFactionB, 0);
            }

            if (playerJoined && player) {
                // Player always on SideA (allied side)
                player->AddToFaction(battleFactionA, 0);

                // Do NOT set kPlayerTeammate — causes cascade (allies defend player
                // against retaliating ally → infighting → guards join → chaos).

                // Enlist nearby guards into battle faction (SideA = ally)
                EnlistFriendlyGuards(joinFaction, battleFactionA, player);

                // Remove player from crime factions (C++ guarantee — runs regardless
                // of Papyrus bytecode state on existing saves)
                RemovePlayerCrimeFactions();
            }

            // Store the normalization so reinforcements use the same side mapping
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (activeBattle_) {
                    activeBattle_->sideAFaction = allyFactionId;
                }
            }

            logger::info("ExecuteFullBattleSpawn: factions assigned — allies({})=SideA, enemies({})=SideB",
                allyFactionId, enemyFactionId);
        } else {
            logger::error("ExecuteFullBattleSpawn: FAILED to resolve battle factions from IntelEngine.esp!");
            out["success"] = false;
            out["error"] = "battle factions not found in IntelEngine.esp";
            return out.dump();
        }

        // --- Step 5b: Hostile standing check ---
        // If player has very negative standing (<= -40) with a faction,
        // add player to the OPPOSING battle faction so soldiers attack them.
        auto* fp = FactionPolitics::GetSingleton();
        if (!playerJoined && fp) {
            int standingA = fp->GetPlayerStanding(factionA);
            int standingB = fp->GetPlayerStanding(factionB);

            // Only add player to ONE opposing faction (pick the worse standing).
            // Adding to both would put the player in two mutually hostile factions.
            // Note: after normalization, SideA=allyish SideB=enemyish, but without
            // playerJoin neither side is truly "allied" — use standing to decide.
            if (standingA <= HOSTILE_STANDING_THRESHOLD || standingB <= HOSTILE_STANDING_THRESHOLD) {
                if (standingA <= standingB && standingA <= HOSTILE_STANDING_THRESHOLD && battleFactionB) {
                    player->AddToFaction(battleFactionB, 0);
                    logger::info("ExecuteFullBattleSpawn: player hostile to {} (standing {}), added to opposing faction",
                        factionA, standingA);
                } else if (standingB <= HOSTILE_STANDING_THRESHOLD && battleFactionA) {
                    player->AddToFaction(battleFactionA, 0);
                    logger::info("ExecuteFullBattleSpawn: player hostile to {} (standing {}), added to opposing faction",
                        factionB, standingB);
                }
            }
        }

        // --- Step 6: Select leader (first allied soldier) ---
        // Use the same ally determination as the normalization block above
        RE::FormID leaderFormId = 0;
        if (playerJoined) {
            auto& alliedSoldiers = (joinFaction == factionA) ? soldiersA : soldiersB;
            if (!alliedSoldiers.empty() && alliedSoldiers[0]) {
                auto* leader = alliedSoldiers[0];
                leaderFormId = leader->GetFormID();
                // Make leader Protected (per-actor flag, not shared ActorBase)
                auto& boolFlags = leader->GetActorRuntimeData().boolFlags;
                boolFlags.set(RE::Actor::BOOL_FLAGS::kProtected);
                leader->AsActorValueOwner()->SetActorValue(RE::ActorValue::kSpeedMult, 115.f);
                logger::info("ExecuteFullBattleSpawn: leader {:08X} — {}", leaderFormId,
                    leader->GetDisplayFullName());
            }
        }

        // --- Step 7: Snapshot bounties before combat starts ---
        SnapshotBounties();

        int paired = static_cast<int>(std::min(soldiersA.size(), soldiersB.size()));
        logger::info("ExecuteFullBattleSpawn: {} vs {}, {} combat pairs, playerJoined={}",
                    soldiersA.size(), soldiersB.size(), paired, playerJoined);

        out["success"] = true;
        out["sideACount"] = static_cast<int>(soldiersA.size());
        out["sideBCount"] = static_cast<int>(soldiersB.size());
        out["paired"] = paired;
        out["playerJoined"] = playerJoined;
        out["joinFaction"] = joinFaction;
        out["notification"] = joinNotification;
        out["leaderFormId"] = nlohmann::json::array({leaderFormId});

        nlohmann::json sideAIds = nlohmann::json::array();
        for (auto* a : soldiersA) sideAIds.push_back(a->GetFormID());
        nlohmann::json sideBIds = nlohmann::json::array();
        for (auto* b : soldiersB) sideBIds.push_back(b->GetFormID());
        out["sideAFormIds"] = sideAIds;
        out["sideBFormIds"] = sideBIds;

        return out.dump();
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

    std::vector<RE::Actor*> BattleManager::SpawnSoldiersForFaction(
            const std::string& factionId, RE::TESFaction* battleFaction,
            int count, RE::TESObjectREFR* spawnRef, RE::Actor* player,
            float offsetX, float offsetY, float centerX, float centerY) {
        std::vector<RE::Actor*> soldiers;
        auto* fp = FactionPolitics::GetSingleton();

        auto templateId = fp->GetSoldierTemplate(factionId);
        RE::TESBoundObject* base = nullptr;
        if (!templateId.empty()) {
            auto* form = RE::TESForm::LookupByEditorID(templateId);
            if (form) base = form->As<RE::TESBoundObject>();
        }
        if (!base) {
            auto* fallback = RE::TESForm::LookupByEditorID("LvlBanditMeleeAny");
            if (fallback) base = fallback->As<RE::TESBoundObject>();
            if (!base) {
                logger::error("SpawnSoldiersForFaction: no template for {} and fallback failed", factionId);
                return soldiers;
            }
            logger::warn("SpawnSoldiersForFaction: no template for {}, using LvlBanditMeleeAny", factionId);
        }

        // Resolve leveled list to concrete NPC forms
        std::vector<RE::TESBoundObject*> resolved;
        if (base->GetFormType() == RE::FormType::LeveledNPC) {
            auto playerLevel = player->GetLevel();
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
        } else {
            for (int i = 0; i < count; ++i) resolved.push_back(base);
        }

        // Spawn + configure
        bool useOffset = (centerX != 0.f || centerY != 0.f);
        for (size_t i = 0; i < resolved.size(); ++i) {
            auto spawnedPtr = spawnRef->PlaceObjectAtMe(resolved[i], true);
            if (!spawnedPtr) continue;
            auto* actor = spawnedPtr.get()->As<RE::Actor>();
            if (!actor) continue;

            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kAggression, 1.f);
            actor->AsActorValueOwner()->SetActorValue(RE::ActorValue::kConfidence, 4.f);
            if (battleFaction) actor->AddToFaction(battleFaction, 0);

            // Reposition if offsets provided (wave 1 at anchor, reinforcements at player)
            if (useOffset && spawnRef != player) {
                float finalX = centerX + offsetX + RandomFloat(-50.f, 50.f);
                float finalY = centerY + offsetY + RandomFloat(-50.f, 50.f);
                actor->SetPosition(RE::NiPoint3{finalX, finalY, spawnRef->GetPositionZ()}, true);
            }

            RegisterActor(actor, factionId, 0);
            soldiers.push_back(actor);
        }

        logger::info("SpawnSoldiersForFaction: spawned {}/{} for {} at ({:.0f}, {:.0f})",
                    soldiers.size(), count, factionId,
                    centerX + offsetX, centerY + offsetY);
        return soldiers;
    }

    std::string BattleManager::SpawnReinforcements(int count, RE::Actor* player,
                                                     RE::TESObjectREFR* spawnAnchor) {
        nlohmann::json out;
        out["success"] = false;

        if (!player || count <= 0) {
            out["error"] = "invalid args";
            return out.dump();
        }

        std::string factionA, factionB, playerSide, sideAFaction;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) {
                out["error"] = "no active battle";
                return out.dump();
            }
            factionA = activeBattle_->factionA;
            factionB = activeBattle_->factionB;
            playerSide = activeBattle_->playerSide;
            sideAFaction = activeBattle_->sideAFaction;
        }

        auto* fp = FactionPolitics::GetSingleton();

        // Resolve ESP battle factions
        auto [battleFactionA, battleFactionB] = ResolveBattleFactions();
        if (!battleFactionA || !battleFactionB) {
            out["error"] = "battle factions not found";
            return out.dump();
        }

        // Use the same side normalization as the initial spawn:
        // sideAFaction was assigned to Intel_BattleSideA, the other to SideB.
        // Default to factionA=SideA if sideAFaction wasn't set (pre-existing battle).
        bool aIsSideA = sideAFaction.empty() || (sideAFaction == factionA);
        const auto& sideAId = aIsSideA ? factionA : factionB;
        const auto& sideBId = aIsSideA ? factionB : factionA;

        // Spawn at anchor (battle location) if available, otherwise at player
        RE::TESObjectREFR* spawnRef = (spawnAnchor && spawnAnchor->Is3DLoaded()) ? spawnAnchor : player;
        if (spawnRef != player) {
            logger::info("SpawnReinforcements: spawning at anchor {:08X}", spawnRef->GetFormID());
        }
        auto soldiersA = SpawnSoldiersForFaction(sideAId, battleFactionA, count, spawnRef, player);
        auto soldiersB = SpawnSoldiersForFaction(sideBId, battleFactionB, count, spawnRef, player);

        // Post-spawn validation: confirm battle wasn't ended during spawn
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) {
                logger::warn("SpawnReinforcements: battle ended during spawn — cleaning up {} orphans",
                    soldiersA.size() + soldiersB.size());
                for (auto* a : soldiersA) { if (a) a->Disable(); }
                for (auto* b : soldiersB) { if (b) b->Disable(); }
                out["error"] = "battle ended during spawn";
                return out.dump();
            }
        }

        // Start combat between new soldiers
        for (size_t i = 0; i < std::min(soldiersA.size(), soldiersB.size()); ++i) {
            if (soldiersA[i] && soldiersB[i]) {
                soldiersA[i]->GetActorRuntimeData().currentCombatTarget = soldiersB[i]->GetHandle();
                soldiersB[i]->GetActorRuntimeData().currentCombatTarget = soldiersA[i]->GetHandle();
            }
        }

        // kPlayerTeammate NOT set — causes cascade (see ExecuteFullBattleSpawn comment)

        // Remove crime factions from reinforcements (reuse bounty prevention)
        SnapshotBounties();

        logger::info("SpawnReinforcements: {}/{} A, {}/{} B spawned",
            soldiersA.size(), count, soldiersB.size(), count);

        out["success"] = true;
        out["sideACount"] = static_cast<int>(soldiersA.size());
        out["sideBCount"] = static_cast<int>(soldiersB.size());
        out["playerSide"] = playerSide;

        nlohmann::json sideAIds = nlohmann::json::array();
        for (auto* a : soldiersA) sideAIds.push_back(a->GetFormID());
        nlohmann::json sideBIds = nlohmann::json::array();
        for (auto* b : soldiersB) sideBIds.push_back(b->GetFormID());
        out["sideAFormIds"] = sideAIds;
        out["sideBFormIds"] = sideBIds;

        return out.dump();
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
        return out.dump();
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
        // Instead of snapshot/restore, prevent bounty by removing hold crime factions
        // from battle soldiers. Killing soldiers outside a crime faction generates no bounty.
        // This is called from ExecuteFullBattleSpawn after soldiers are spawned.

        std::vector<RE::FormID> soldierFormIds;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!activeBattle_) return;
            for (const auto& a : activeBattle_->actors) {
                soldierFormIds.push_back(a.formId);
            }
        }

        int removed = 0;
        for (auto fid : kCrimeFactionIds) {
            auto* crimeFaction = RE::TESForm::LookupByID<RE::TESFaction>(fid);
            if (!crimeFaction) continue;
            for (auto soldierFid : soldierFormIds) {
                auto* actor = RE::TESForm::LookupByID<RE::Actor>(soldierFid);
                if (actor) {
                    // rank -1 = remove from faction (overrides base NPC template)
                    actor->AddToFaction(crimeFaction, -1);
                    removed++;
                }
            }
        }

        logger::info("[BattleManager] Bounty prevention: removed crime factions from {} soldier-faction pairs", removed);
    }

    // ClearBattleBounties and RestoreBounties — REMOVED.
    // Bounty prevention handled at spawn time by removing crime factions from soldiers.

}  // namespace IntelEngine
