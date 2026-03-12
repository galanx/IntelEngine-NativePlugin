#pragma once

/**
 * PoliticalDB - IntelEngine-owned SQLite Database
 *
 * Manages IntelEngine.db with all faction politics tables.
 * Completely independent from SkyrimNet's database.
 *
 * Tables:
 *   faction_relations      - Faction-pair relation scores
 *   faction_events         - Political event log
 *   player_faction_standing - Player standing per faction
 *   faction_wars           - Active/historical wars
 */

#include "Plugin.h"

#include <sqlite3.h>
#include <mutex>
#include <string>
#include <vector>
#include <optional>

namespace IntelEngine {

    struct FactionRelation {
        std::string factionA;
        std::string factionB;
        int relationScore = 0;
        bool tradeActive = false;
        bool warActive = false;
    };

    struct FactionEvent {
        int id = 0;
        std::string factionA;
        std::string factionB;
        std::string eventType;
        std::string description;
        int relationDelta = 0;
        float gameTime = 0.0f;
        std::string instigatorNpc;
    };

    struct PlayerStanding {
        std::string factionId;
        int standing = 0;
        std::string title;
        float lastChangeTime = 0.0f;
    };

    struct FactionWar {
        int id = 0;
        std::string factionA;
        std::string factionB;
        float startTime = 0.0f;
        float endTime = 0.0f;
        std::string victor;
        int battlesFought = 0;
        int factionAMorale = 100;
        int factionBMorale = 100;
        int factionAStrength = 100;
        int factionBStrength = 100;
    };

    struct BattleRow {
        int id = 0;
        int warId = 0;
        std::string locationName;
        float gameTime = 0.0f;
        std::string attacker;
        std::string defender;
        std::string result;
        int attackerLosses = 0;
        int defenderLosses = 0;
        std::string narrative;
    };

    class PoliticalDB {
    public:
        static PoliticalDB* GetSingleton() {
            static PoliticalDB instance;
            return &instance;
        }

        /** Open/create database at the given path and ensure all tables exist. */
        bool Initialize(const std::string& dbPath);

        /** Close the database connection. */
        void Shutdown();

        /** Check if the database is open and ready. */
        bool IsReady() const { return db_ != nullptr; }

        /** Delete all rows from political tables where game_time > currentGameTime.
         *  Used on save load to remove future events from save-scumming.
         *  Returns total rows deleted across all tables. */
        int CleanupFutureEvents(float currentGameTime);

        // =================================================================
        // Faction Relations
        // =================================================================

        /** Get relation between two factions. Returns 0 if not found. */
        int GetRelation(const std::string& factionA, const std::string& factionB);

        /** Set relation score between two factions. Creates row if needed. */
        bool SetRelation(const std::string& factionA, const std::string& factionB, int score);

        /** Adjust relation by delta (clamped to RELATION_MIN..RELATION_MAX). Returns new score. */
        int AdjustRelation(const std::string& factionA, const std::string& factionB, int delta);

        /** Get all faction relations. */
        std::vector<FactionRelation> GetAllRelations();

        // =================================================================
        // Faction Events
        // =================================================================

        /** Record a political event. Returns the event ID. */
        int RecordEvent(const std::string& factionA, const std::string& factionB,
                        const std::string& eventType, const std::string& description,
                        int relationDelta, float gameTime,
                        const std::string& instigatorNpc = "");

        /** Get recent events (most recent first). */
        std::vector<FactionEvent> GetRecentEvents(int maxCount);

        /** Get all events in chronological order (oldest first). Used for score recalculation. */
        std::vector<FactionEvent> GetAllEventsChronological();

        // =================================================================
        // Player Standing
        // =================================================================

        /** Get player standing with a faction. Returns 0 if not found. */
        int GetPlayerStanding(const std::string& factionId);

        /** Adjust player standing by delta (clamped RELATION_MIN..RELATION_MAX). Returns new standing. */
        int AdjustPlayerStanding(const std::string& factionId, int delta, float gameTime);

        /** Get all player standings. */
        std::vector<PlayerStanding> GetAllPlayerStandings();

        // =================================================================
        // Wars
        // =================================================================

        /** Get active war between two factions (if any). */
        std::optional<FactionWar> GetActiveWar(const std::string& factionA, const std::string& factionB);

        /** Get all active wars. */
        std::vector<FactionWar> GetActiveWars();

        /** Start a new war between two factions. Returns war ID or -1 on failure. */
        int StartWar(const std::string& factionA, const std::string& factionB,
                     float startTime, int strengthA = 100, int strengthB = 100);

        /** Update morale and strength for an active war. */
        bool UpdateWarState(int warId, int moraleA, int moraleB, int strengthA, int strengthB, int battlesFought);

        /** End a war by setting victor and end_time. */
        bool EndWar(int warId, const std::string& victor, float endTime);

        /** Record an off-screen battle result. Returns battle ID or -1 on failure. */
        int RecordBattle(int warId, const std::string& locationName, float gameTime,
                         const std::string& attacker, const std::string& defender,
                         const std::string& result, int attackerLosses, int defenderLosses,
                         const std::string& narrative);

        /** Get recent battles for a war (most recent first). */
        std::vector<BattleRow> GetBattlesForWar(int warId, int maxCount = 5);

        /** Get the most recent war between two factions (active or ended). */
        std::optional<FactionWar> GetMostRecentWar(const std::string& factionA, const std::string& factionB);

        // =================================================================
        // Initialization Helpers
        // =================================================================

        /** Seed default relations from factions.yaml config. Only inserts if row doesn't exist. */
        bool SeedDefaultRelation(const std::string& factionA, const std::string& factionB, int score);

        /** Reset all relation_score values to 0. Used before score recalculation. */
        bool ResetAllRelationScores();

    private:
        PoliticalDB() = default;
        ~PoliticalDB() { Shutdown(); }
        PoliticalDB(const PoliticalDB&) = delete;
        PoliticalDB& operator=(const PoliticalDB&) = delete;

        /** Create all tables if they don't exist. */
        bool CreateTables();

        /** Order faction IDs alphabetically for consistent storage. */
        static std::pair<std::string, std::string> OrderFactions(
            const std::string& a, const std::string& b);

        /** Execute a simple SQL statement (no results). */
        bool Execute(const std::string& sql);

        /** Upsert relation score for an already-ordered faction pair. Must be called under mutex_. */
        bool UpsertRelationScore(const std::string& orderedA, const std::string& orderedB, int score);

        sqlite3* db_ = nullptr;
        mutable std::mutex mutex_;
    };

}  // namespace IntelEngine
