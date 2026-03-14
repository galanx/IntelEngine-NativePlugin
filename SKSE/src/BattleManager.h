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

        /** Check if an actor is still alive in the game world. */
        bool IsActorAlive(RE::FormID formId) const;

        std::optional<BattleState> activeBattle_;
        mutable std::mutex mutex_;
        int nextBattleId_ = 1;

        // Pending battles (location-based, waiting for player proximity)
        std::vector<PendingBattle> pendingBattles_;
        mutable std::mutex pendingMutex_;  // separate mutex — pending ops don't touch active battle
        int nextPendingId_ = 1;
        static constexpr float PENDING_TRIGGER_DISTANCE = 5000.f;
        static constexpr float PENDING_BATTLE_DURATION = 3.0f / 24.0f;  // 3 game hours in days

        // Expired battle results queue — consumed one-at-a-time by Papyrus for RESULT notifications
        std::vector<std::string> expiredResults_;  // JSON strings, popped front per read
    };

}  // namespace IntelEngine
