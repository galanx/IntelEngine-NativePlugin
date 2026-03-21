#pragma once

/**
 * BattleManager - Player-Present Battle System
 *
 * Tracks a single active battle: spawned actors, wave progress, morale,
 * and narration events. Papyrus polls PollBattleState() every 3 seconds
 * to get events and drive the battle forward.
 *
 * Only ONE battle at a time — Skyrim's engine can't handle multiple
 * mass combats simultaneously.
 */

#include "Plugin.h"

#include <nlohmann/json.hpp>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_set>

namespace IntelEngine {

    /** A battle waiting to materialize — exists as metadata until the player approaches. */
    struct PendingBattle {
        int id = 0;
        std::string factionA;
        std::string factionB;
        std::string locationName;
        float x = 0.f, y = 0.f, z = 0.f;  // world coordinates from BGSLocation worldLocMarker
        float deadline = 0.f;               // game time when battle auto-resolves
        std::string resultJson;             // full DM response JSON for post-resolution
    };

    class BattleManager {
    public:
        static BattleManager* GetSingleton() {
            static BattleManager instance;
            return &instance;
        }

        // =================================================================
        // Pending Battles (location-based, spawn on player proximity)
        // =================================================================

        /** Create a pending battle at a named location's world coordinates.
         *  Returns pending battle ID (>= 0) or -1 on failure (location not found). */
        int AddPendingBattle(const std::string& locationName,
                             const std::string& factionA, const std::string& factionB,
                             const std::string& resultJson);

        /** Check all pending battles against player position.
         *  Returns the ID of the first pending battle within triggerDistance, or -1.
         *  Also expires battles past their deadline (calls RecordOffScreenBattle). */
        int PollPendingBattles(float playerX, float playerY, float playerZ);

        /** Remove a pending battle by ID (after it has been triggered or expired). */
        void RemovePendingBattle(int id);

        /** Get info about a pending battle as JSON string. Returns "{}" if not found. */
        std::string GetPendingBattleInfo(int id) const;

        /** Get count of active pending battles. */
        int GetPendingBattleCount() const;

        /** Clear all pending battles and expired results. Called on game reload. */
        void ClearPendingBattles();

        /** Get and consume one expired battle result JSON from the queue.
         *  Returns "" (empty string) if no expired battles queued.
         *  Called by Papyrus each poll cycle; drains one result per call. */
        std::string GetLastExpiredBattleResult();

        // =================================================================
        // Battle Lifecycle
        // =================================================================

        /** Start a new battle. Returns battle ID or -1 if a battle is already active. */
        int StartBattle(const std::string& factionA, const std::string& factionB,
                        const std::string& locationName, int warId);

        /** End the active battle. Records result. */
        void EndBattle(int battleId, const std::string& result, const std::string& victor);

        /** Check if a battle is currently active. */
        bool IsBattleActive() const;

        /** Get the active battle ID, or -1 if none. */
        int GetActiveBattleId() const;

        // =================================================================
        // Actor Tracking
        // =================================================================

        /** Register a spawned actor in the battle.
         *  tier: 0=generic soldier, 1=recruited NPC, 2=faction leader */
        bool RegisterActor(RE::Actor* actor, const std::string& factionId, int tier);

        /** Get count of alive actors for a faction. */
        int GetAliveCount(const std::string& factionId) const;

        // =================================================================
        // Morale
        // =================================================================

        /** Get current morale for a faction in the active battle (0-100). */
        int GetMorale(const std::string& factionId) const;

        /** Adjust morale by delta (clamped 0-100). */
        void AdjustMorale(const std::string& factionId, int delta);

        // =================================================================
        // Combat Polling (called every 3s from Papyrus)
        // =================================================================

        /** Poll current battle state. Returns JSON with events since last poll.
         *  Checks: actor deaths, morale thresholds, wave timing, battle end.
         *  Returns "{}" if no battle active. */
        std::string PollBattleState();

        // =================================================================
        // Wave Management
        // =================================================================

        /** Get current wave number (0=pre-battle, 1=vanguard, 2=reinforcements, 3=reserves). */
        int GetCurrentWave() const;

        /** Advance to next wave. Called by Papyrus after spawning wave actors. */
        void AdvanceWave();

        // =================================================================
        // Player Participation
        // =================================================================

        /** Set the player's battle side. Applies morale boost/penalty.
         *  factionId must match factionA or factionB. Empty string = leave/spectator. */
        bool SetPlayerSide(const std::string& factionId);

        /** Get the player's current battle side (empty = spectator). */
        std::string GetPlayerSide() const;

        /** Check if the player participated in the current battle at any point. */
        bool HasPlayerParticipated() const;

        /** Check if the given faction is involved in the active battle.
         *  Used by CheckCrimeGoldStandings to exempt battle kills. */
        bool IsBattleFaction(const std::string& factionId) const;

        // =================================================================
        // Phase 1 Migration: Logic moved from Papyrus to C++
        // =================================================================

        /** Finalize a battle: apply standings, record political events, build narrative.
         *  Returns JSON with all results for Papyrus to display notifications and run cleanup.
         *  Consolidates: HandleBattleEnd, ApplyPlayerKillStanding, ApplyPostBattleStanding,
         *  ApplySpectatorConsequences, InjectBattleWitnessMemories, narrative building. */
        std::string FinalizeBattle(int battleId, const std::string& result,
                                   const std::string& victor, int deadA, int deadB,
                                   const std::string& locationName, float gameTime);

        /** Calculate reinforcement spawn positions behind player relative to battle center.
         *  Returns JSON: {soldierCount, spawnAX/AY, spawnBX/BY, spawnZ} */
        std::string CalculateReinforcementPositions(float playerX, float playerY, float playerZ,
                                                     float centerX, float centerY, int waveNum);

        /** Evaluate whether player should auto-join a battle side.
         *  Returns JSON: {shouldJoin, joinFaction, displayName, isQuestJoin} */
        std::string EvaluatePlayerJoin(const std::string& questAutoJoinFaction);

        /** Get notification text for a battle event.
         *  type: "wave1", "wave2", "wave3", "no_survivor_win", "no_survivor_loss",
         *        "soldier_victory", "soldier_defeat" */
        std::string GetBattleNotification(const std::string& type,
                                          const std::string& locationName = "",
                                          const std::string& victorName = "",
                                          bool playerWon = false);

        /** Validate that a faction_battle quest can be dispatched.
         *  Returns JSON: {canStart, enemyFaction, failReason} */
        std::string ValidateFactionBattleDispatch(const std::string& alliedFaction,
                                                   const std::string& suggestedEnemy = "");

        /** Spawn reinforcements for an active battle (wave 2+).
         *  Handles factions, aggression, combat pairs, crime faction removal.
         *  Returns JSON: {success, sideACount, sideBCount, sideAFormIds, sideBFormIds, playerSide} */
        std::string SpawnReinforcements(int count, RE::Actor* player,
                                        RE::TESObjectREFR* spawnAnchor = nullptr);

        /** Calculate mid-battle state for late-arriving player.
         *  Returns JSON: {soldiersPerSide, moraleLossA, moraleLossB} */
        std::string CalculateMidBattleState(float scheduledTime, float currentTime);

        /** Get action to take from poll result. Parses stateJson internally.
         *  Returns JSON: {action: "none"|"spawn_wave"|"battle_end", waveNum, result, victor} */
        std::string GetPollAction(const std::string& stateJson);

        /** Reset all battle state. Called after cleanup completes. */
        void ResetBattleState();

        /** Called every Papyrus poll — clears bounty and stops hostile friendly guards. */
        void SuppressBountyTick();

        /** Enlist nearby friendly guards into the battle (add to battle faction, remove crime factions).
         *  Tracked in modifiedGuardFormIds_ for reliable cleanup. */
        void EnlistFriendlyGuards(const std::string& playerFactionId, RE::TESFaction* battleFaction, RE::Actor* player);

        /** Restore all enlisted guards to pre-battle state (remove battle faction, restore crime factions).
         *  Uses tracked FormIDs — works even if guards unloaded. */
        void RestoreEnlistedGuards();

        /** Clean up ALL stale battle state — called on game load as safety net. */
        void CleanupStaleBattleState();

        /** Remove player from hold crime factions (prevents ALL bounty).
         *  Called at faction quest start. Stays removed until RestorePlayerCrimeFactions. */
        void RemovePlayerCrimeFactions();

        /** Restore player to hold crime factions and clear residual bounty.
         *  Called when faction quest fully completes. */
        void RestorePlayerCrimeFactions();

        /** Execute the ENTIRE battle spawn sequence in C++.
         *  Handles: player join, position calculation, spawn both sides,
         *  faction assignment, combat initiation setup.
         *  spawnAnchor: quest location ObjectReference — soldiers spawn HERE, not at player.
         *  Returns JSON: {success, sideACount, sideBCount, playerJoined, joinFaction,
         *                  notification, leaderFormId, sideAFormIds, sideBFormIds}
         *  Papyrus only does: SetPlayerTeammate, ForceRefTo (quest marker). */
        std::string ExecuteFullBattleSpawn(const std::string& questAutoJoinFaction,
                                           RE::Actor* player, float playerAngleZ,
                                           RE::TESObjectREFR* spawnAnchor);

        /** Get FormIDs of soldiers on a given side ("A" or "B").
         *  Returns JSON: {"formIds": [...], "count": N, "alive": M} */
        std::string GetBattleSoldierFormIds(const std::string& side) const;

        /** Set all alive soldiers on a side as player teammates (or clear).
         *  Used when player manually joins mid-battle. */
        void SetBattleSoldiersAsTeammates(const std::string& side, bool isTeammate = true);

        /** Count dead soldiers on a given side ("A" or "B"). */
        int CountDeadSoldiers(const std::string& side) const;

        /** Cleanup battle soldiers by proximity to player.
         *  Disables+deletes actors behind the player and far enough away.
         *  Returns count of remaining (not yet cleaned) actors. */
        int CleanupBattleSoldiers(float playerX, float playerY, float playerZ,
                                   float playerAngleZ, bool forceAll);

        /** Force cleanup ALL battle soldiers (hard timeout). Disables+deletes all tracked actors. */
        void ForceCleanupAllSoldiers();

        /** Remove crime factions from battle soldiers (no bounty for kills). Called at spawn. */
        void SnapshotBounties();

        /** Calculate battle marker offset position for exterior placement.
         *  Returns JSON: {x, y, z} */
        std::string CalculateBattleMarkerPosition(float playerX, float playerY,
                                                   float locX, float locY, float locZ,
                                                   float offsetUnits);

        /** Snapshot of battle state for cross-system queries (single lock acquisition). */
        struct BattleSnapshot {
            bool active = false;
            bool playerParticipated = false;
            std::string factionA;
            std::string factionB;
        };

        /** Get an atomic snapshot of battle state. Used by FactionPolitics to avoid
         *  multiple mutex acquisitions and lock-order issues with configMutex_. */
        BattleSnapshot GetBattleSnapshot() const;

    private:
        BattleManager() = default;
        ~BattleManager() = default;
        BattleManager(const BattleManager&) = delete;
        BattleManager& operator=(const BattleManager&) = delete;

        struct BattleActor {
            RE::FormID formId = 0;
            std::string factionId;
            int tier = 0;            // 0=generic, 1=recruited, 2=leader
            bool alive = true;
        };

        struct BattleState {
            int id = 0;
            int warId = 0;
            std::string factionA;
            std::string factionB;
            std::string locationName;
            int moraleA = 100;
            int moraleB = 100;
            int currentWave = 0;         // 0=pre-battle, 1/2/3 = wave number
            int waveStartAliveA = 0;     // alive count at start of current wave (casualty baseline)
            int waveStartAliveB = 0;
            // Narration throttling — track which events have been narrated
            bool firstBloodNarrated = false;
            std::unordered_set<std::string> moraleThresholdsNarrated; // "factionId_threshold" keys

            std::vector<BattleActor> actors;

            // Player participation
            std::string playerSide;          // empty = spectator, factionId = participating
            bool playerParticipated = false;  // true if player joined at any point (survives side changes)
            int playerKillsA = 0;            // soldiers from factionA killed by player
            int playerKillsB = 0;            // soldiers from factionB killed by player
        };

        /** Spawn soldiers for one faction side. Shared by ExecuteFullBattleSpawn and SpawnReinforcements. */
        std::vector<RE::Actor*> SpawnSoldiersForFaction(
            const std::string& factionId, RE::TESFaction* battleFaction,
            int count, RE::TESObjectREFR* spawnRef, RE::Actor* player,
            float offsetX = 0.f, float offsetY = 0.f, float centerX = 0.f, float centerY = 0.f);

        /** Resolve Intel_BattleSideA and Intel_BattleSideB factions from IntelEngine.esp.
         *  Returns {nullptr, nullptr} if ESP not found. */
        std::pair<RE::TESFaction*, RE::TESFaction*> ResolveBattleFactions() const;

        /** Thread-safe random float in [min, max]. */
        static float RandomFloat(float min, float max);

        /** Check if an actor is still alive in the game world. */
        bool IsActorAlive(RE::FormID formId) const;

        std::optional<BattleState> activeBattle_;
        mutable std::mutex mutex_;
        int nextBattleId_ = 1;
        std::atomic<bool> suppressBattleBounty_{false};  // set during active battle with player

        // Non-battle actors modified during battle (guards added to battle faction + teammate).
        // Tracked for reliable cleanup — ForEachLoadedActor misses unloaded actors.
        std::vector<RE::FormID> modifiedGuardFormIds_;
        bool playerCrimeFactionsRemoved_ = false;  // track if player needs crime faction restore

        // Pending battles (location-based, waiting for player proximity)
        std::vector<PendingBattle> pendingBattles_;
        mutable std::mutex pendingMutex_;  // separate mutex — pending ops don't touch active battle
        int nextPendingId_ = 1;
        static constexpr float PENDING_TRIGGER_DISTANCE = 5000.f;
        static constexpr float PENDING_BATTLE_DURATION = 3.0f / 24.0f;  // 3 game hours in days

        // Standing constants (MUST match Papyrus properties in IntelEngine_Battle.psc lines 40-47)
        static constexpr int KILL_STANDING_PENALTY_PER_SOLDIER = -5;
        static constexpr int VICTORY_ALLY_BONUS = 15;
        static constexpr int VICTORY_ENEMY_PENALTY = -10;
        static constexpr int DEFEAT_ALLY_BONUS = 5;
        static constexpr int DEFEAT_ENEMY_PENALTY = -5;
        static constexpr int SPECTATOR_PENALTY = -5;
        static constexpr int SPECTATOR_PENALTY_THRESHOLD = 10;
        static constexpr int AUTO_JOIN_STANDING_THRESHOLD = 20;
        static constexpr int HOSTILE_STANDING_THRESHOLD = -40;  // faction attacks player on sight

        // Wave soldier counts — MUST match Papyrus GetWaveSoldierCount() in IntelEngine_Battle.psc
        static constexpr int WAVE1_SOLDIERS = 6;   // vanguard
        static constexpr int WAVE2_SOLDIERS = 5;   // first reinforcements
        static constexpr int WAVE3_SOLDIERS = 4;   // second reinforcements
        static constexpr int WAVE4_SOLDIERS = 4;   // reserves
        static constexpr int WAVE5_SOLDIERS = 3;   // last stand
        static constexpr int MAX_SOLDIERS_PER_SIDE = 22;

        // Spawn distances
        static constexpr float SPAWN_DISTANCE = 400.f;
        static constexpr float REINFORCEMENT_BEHIND_DISTANCE = 600.f;
        static constexpr float BATTLE_MARKER_OFFSET = 3000.f;

        // Expired battle results queue — consumed one-at-a-time by Papyrus for RESULT notifications
        std::vector<std::string> expiredResults_;  // JSON strings, popped front per read

        // Persistent cleanup list — survives EndBattle/ResetBattleState.
        // Populated when battle ends, cleared when physical cleanup completes.
        std::vector<RE::FormID> cleanupFormIds_;
        mutable std::mutex cleanupMutex_;

        // Hold crime faction FormIDs (Skyrim.esm) — removed from soldiers to prevent bounty
        static constexpr RE::FormID kCrimeFactionIds[] = {
            0x00029DB0,  // Whiterun
            0x00029DB1,  // Rift
            0x00029DB2,  // Reach
            0x00029DB3,  // Eastmarch
            0x00029DB4,  // Haafingar
            0x00029DB5,  // Hjaalmarch
            0x00029DB6,  // Pale
            0x00029DB7,  // Falkreath
            0x00029DB8   // Winterhold
        };
    };

}  // namespace IntelEngine
