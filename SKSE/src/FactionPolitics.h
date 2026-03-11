#pragma once

/**
 * FactionPolitics - Political System Singleton
 *
 * Loads faction config from factions.yaml, builds LLM context for the
 * Political DM, validates events, and provides Papyrus-callable queries.
 *
 * Owns no database state — delegates all persistence to PoliticalDB.
 * Owns no SkyrimNet state — uses existing PublicAPI for NPC memory injection.
 */

#include "Plugin.h"
#include "PoliticalDB.h"

#include <nlohmann/json.hpp>
#include <mutex>
#include <optional>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>

namespace IntelEngine {

    // Relation score thresholds for diplomatic status tiers
    constexpr int RELATION_ALLIANCE   =  80;  // 80-100: Alliance
    constexpr int RELATION_FRIENDLY   =  40;  // 40-79:  Friendly
    constexpr int RELATION_NEUTRAL    =   0;  //  0-39:  Neutral
    constexpr int RELATION_TENSE      = -39;  // -39 to -1: Tense
    constexpr int RELATION_HOSTILE    = -49;  // -49 to -40: Hostile
    // Below -49: War (per-faction war_threshold overrides this)

    constexpr int RELATION_MIN = -100;
    constexpr int RELATION_MAX =  100;

    // Safety limits for LLM-generated content stored in DB
    constexpr size_t MAX_EVENT_DESCRIPTION_LENGTH = 2000;
    constexpr int MAX_EVENT_REPLAY_LIMIT = 10000;

    struct FactionConfig {
        std::string id;
        std::string name;
        std::string type;           // military, guild, political
        std::string hold;
        std::string skyrimFactionId; // Skyrim engine faction editor ID (e.g. "CWSonsFaction") for NPC membership checks
        std::vector<std::string> leaderNames;
        std::vector<std::string> rivals;
        std::vector<std::string> allies;
        int baseArmyStrength = 0;   // Used in Political DM context for event generation
        int warThreshold = -50;
        std::string conflictStyle;  // brawl, sabotage, assassination, proxy — used in Political DM context
    };

    struct DefaultRelation {
        std::string factionA;
        std::string factionB;
        int relation = 0;
    };

    class FactionPolitics {
    public:
        static FactionPolitics* GetSingleton() {
            static FactionPolitics instance;
            return &instance;
        }

        /** Load factions.yaml and seed default relations into PoliticalDB. */
        void Initialize();

        /** Reload factions.yaml (hot-reload support). */
        void Reload() { Initialize(); }

        /** Check if the system is initialized and ready. */
        bool IsReady() const { return initialized_.load(); }

        /** Clear in-memory caches (called on save load / timeline cleanup). */
        void ClearCaches();

        // =================================================================
        // Config Access
        // =================================================================

        /** Get a faction config by ID. Returns nullopt if not found. */
        std::optional<FactionConfig> GetFaction(const std::string& factionId) const;

        /** Get all loaded factions (returns copy — safe across threads). */
        std::vector<FactionConfig> GetAllFactions() const {
            std::lock_guard<std::mutex> lock(configMutex_);
            return factions_;
        }

        /** Get faction ID by display name (case-insensitive). Empty if not found. */
        std::string GetFactionIdByName(const std::string& displayName) const;

        // =================================================================
        // Political DM Context Building
        // =================================================================

        /** Build the full context JSON string for the Political DM prompt.
         *  Includes: all factions, current relations, recent events, active wars,
         *  player standings. */
        std::string BuildPoliticalContext(float currentGameTime);

        // =================================================================
        // Relation Status Helpers
        // =================================================================

        /** Get human-readable status for a relation score. */
        static std::string GetRelationStatus(int score);

        /** Check if a faction pair is at war (score below per-faction threshold). */
        bool IsAtWar(const std::string& factionA, const std::string& factionB);

        // =================================================================
        // Event Validation
        // =================================================================

        /** Validate a political event from the LLM before recording.
         *  Checks: faction IDs exist, delta within bounds, event type valid. */
        bool ValidateEvent(const std::string& factionA, const std::string& factionB,
                           const std::string& eventType, int relationDelta);

        // =================================================================
        // Dashboard JSON
        // =================================================================

        /** Build JSON for the PoliticsTab dashboard component. */
        std::string BuildDashboardJson();

        /** Build a compact markdown summary for injection into Story DM / NPC DM contexts.
         *  Includes: non-neutral relations, recent events, active wars. Lightweight. */
        std::string BuildPoliticalSummary();

        /** Write political_state.json for pull-based NPC awareness.
         *  Called after each political event. The file is read by the prompt template
         *  via read_json() at conversation time — no UUID issues, works for all NPCs. */
        void WritePoliticalStateFile();

        // =================================================================
        // Politics Settings
        // =================================================================

        bool IsEnabled() const { return enabled_.load(); }
        int GetTickIntervalHours() const { return tickIntervalHours_.load(); }
        int GetMaxRelationChangePerTick() const { return maxRelationChangePerTick_.load(); }
        int GetMaxActiveWars() const { return maxActiveWars_.load(); }

        void SetEnabled(bool val) { enabled_.store(val); }
        void SetTickIntervalHours(int val) { tickIntervalHours_.store(std::clamp(val, 1, 24)); }

        /** Reload politics settings from settings.yaml. */
        void LoadSettings();

        // =================================================================
        // Player Standing Mechanics
        // =================================================================

        /** Process player conduct report with cross-faction consequences.
         *  Applies primary delta to factionId, then checks if the reporting NPC
         *  belongs to a rival/ally faction and applies inverse/matching delta.
         *  Returns number of standings changed (1 or 2). */
        int ProcessPlayerConduct(RE::Actor* reporter, const std::string& factionId,
                                 const std::string& sentiment, const std::string& reason);

        /** Find the political faction ID an NPC belongs to (leader match or engine faction).
         *  Returns empty string if unaffiliated. Only checks tiers 1-2, not location. */
        std::string GetNPCFactionId(RE::Actor* actor) const;

        /** Check crime gold against political factions with skyrim_faction_id.
         *  Compares current crime gold to baseline, applies negative standing
         *  for increases. Returns number of standings changed. */
        int CheckCrimeGoldStandings();

        /** Decay all non-zero player standings by decayRate toward 0.
         *  Returns number of standings decayed. */
        int DecayPlayerStandings(int decayRate);

        /** Snapshot current crime gold values as baseline.
         *  Called on initialization to avoid re-penalizing existing bounties. */
        void SnapshotCrimeGoldBaseline();

    private:
        FactionPolitics() = default;
        ~FactionPolitics() = default;
        FactionPolitics(const FactionPolitics&) = delete;
        FactionPolitics& operator=(const FactionPolitics&) = delete;

        /** Parse factions.yaml into config structs (delegates to FactionConfigLoader). */
        bool LoadConfig();

        /** Seed default relations from config into PoliticalDB. */
        void SeedDefaultRelations();

        /** Recalculate all relation scores from defaults + remaining events.
         *  Called on save load after timeline cleanup to ensure scores match events. */
        void RecalculateRelationScores();

        std::string GetConfigPath() const;

        /** Serialize a FactionConfig to JSON (shared by context/dashboard/state file builders). */
        static nlohmann::json FactionToJson(const FactionConfig& f);

        /** Build faction id→name lookup map (acquires configMutex_ internally). */
        std::unordered_map<std::string, std::string> BuildIdToNameMap() const;

        // Config data (protected by mutex for hot-reload)
        mutable std::mutex configMutex_;
        std::vector<FactionConfig> factions_;
        std::vector<DefaultRelation> defaultRelations_;
        std::unordered_map<std::string, size_t> factionIndex_;  // id -> index in factions_
        std::unordered_map<std::string, std::string> nameToId_; // lowercase name -> id

        // Settings (atomic for lock-free reads from Papyrus thread)
        std::atomic<bool> enabled_{true};
        std::atomic<int> tickIntervalHours_{6};
        std::atomic<int> maxRelationChangePerTick_{15};
        std::atomic<int> maxActiveWars_{2};

        std::atomic<bool> initialized_{false};

        // Crime gold baseline — last-known crime gold per faction (factionId → gold)
        // Used to detect NEW crime gold increases since last check
        std::unordered_map<std::string, int> crimeGoldBaseline_;
    };

}  // namespace IntelEngine
