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
        std::string soldierTemplate; // EditorID of leveled actor list for battle spawning
        std::string prisonLocation;  // Cell EditorID for prisoner holding (Phase 4)
        float prisonMarker[3] = {};  // [x, y, z] position within prison cell (Phase 4)
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

        /** Get soldier template EditorID for a faction. Empty if not configured. */
        std::string GetSoldierTemplate(const std::string& factionId) const;

        /** Get all known faction IDs. Used for fuzzy matching DM suggestions. */
        std::vector<std::string> GetAllFactionIds() const;

        /** Get a rival faction ID for the given faction. Returns first rival, or empty if none. */
        std::string GetFactionRival(const std::string& factionId) const;

        /** Get the faction's current war enemy. If the faction is at war, returns the opponent.
         *  Falls back to GetFactionRival if no active war. */
        std::string GetFactionWarEnemy(const std::string& factionId);

        // =================================================================
        // Convenience wrappers for BattleManager (delegates to PoliticalDB)
        // =================================================================

        /** Get display name for a faction. Returns faction ID if not found. */
        std::string GetFactionDisplayName(const std::string& factionId) const;

        /** Get player's standing with a faction. */
        int GetPlayerStanding(const std::string& factionId) const;

        /** Adjust player's standing with a faction by delta. Returns new standing. */
        int AdjustPlayerStanding(const std::string& factionId, int delta);

        /** Record a political event. Returns event ID. */
        int RecordPoliticalEvent(const std::string& factionA, const std::string& factionB,
                                 const std::string& eventType, const std::string& description,
                                 int delta, float gameTime);

        // =================================================================
        // Phase 2 Migration: Politics logic moved from Papyrus to C++
        // =================================================================

        // =================================================================
        // Phase 3 Migration: StoryEngine helpers
        // =================================================================

        // =================================================================
        // Story Engine helpers (non-faction-specific)
        // =================================================================

        /** Build exclude list from toggle bitmask + environment flags.
         *  Bitmask: bit0=seekPlayer, bit1=informant, bit2=roadEncounter, bit3=ambush,
         *  bit4=stalker, bit5=message, bit6=quest, bit7=factionAmbush,
         *  bit8=questCombat, bit9=questRescue, bit10=questFindItem,
         *  bit11=questFactionCombat, bit12=questFactionRescue, bit13=questFactionBattle
         *  envFlags: bit0=isInterior, bit1=isDangerous */
        static std::string BuildExcludeList(int toggleBitmask, int envFlags);

        /** Validate a DM story response. Returns JSON with validation result.
         *  Checks: type validity, MCM toggles, subtype validity, field presence.
         *  toggleBitmask/envFlags same as BuildExcludeList. */
        static std::string ValidateStoryResponse(const std::string& responseJson,
                                                  int toggleBitmask, int envFlags);

        /** Build faction_battle dispatch fact for the quest giver. */
        std::string BuildFactionBattleDispatchFact(const std::string& alliedFaction,
                                                    const std::string& questLocation,
                                                    const std::string& playerName);

        /** Record faction_battle completion: facts for quest giver + leaders + political event.
         *  Returns JSON with notification text and witnessFact for leaders. */
        std::string RecordFactionBattleCompletion(const std::string& alliedFaction,
                                                   const std::string& questLocation,
                                                   const std::string& playerName,
                                                   const std::string& enemyFaction = "");

        /** Build expiry fact for when player didn't show up. */
        std::string BuildBattleExpiryFact(const std::string& alliedFaction,
                                           const std::string& questLocation,
                                           const std::string& playerName);

        /** Process the Political DM's response. Handles: parsing, validation,
         *  event recording, war declaration, surrender, battle result creation,
         *  standing changes, decay, crime checks. Returns JSON with actions
         *  for Papyrus to execute (fact injection, battle poll, manifestation). */
        std::string ProcessPoliticalDMResponse(const std::string& response, int success);

        /** Run periodic standing mechanics: decay + crime gold checks + write state.
         *  Returns JSON: {decayed, crimeChanges, updated} */
        std::string RunStandingMechanicsInternal();

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

        /** Check if the player is currently at an inn (LocTypeInn keyword). */
        static bool IsPlayerAtInn();

        /** Get the political faction config for an NPC in one lock scope.
         *  Combines GetNPCFactionId + GetFaction to avoid TOCTOU between two mutex acquisitions. */
        std::optional<FactionConfig> GetNPCFaction(RE::Actor* actor) const;

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

        /** Get the most recent witnessable political event (assassination, brawl,
         *  sabotage, espionage, border_skirmish) that happened within the current
         *  tick window. Returns empty string if none. Used by Story DM to hint
         *  at events the player could witness nearby. */
        std::string GetLatestWitnessableEvent();

        /** Check if a political event should physically manifest near the player.
         *  Compares player's hold against involved factions' holds.
         *  Returns JSON spawn instructions if player is at the event location,
         *  empty string if not. Guards: cooldown, no active battle, exterior only. */
        std::string CheckEventManifestation(const std::string& factionA,
                                            const std::string& factionB,
                                            const std::string& eventType);

        /** Confirm manifestation cooldown after Papyrus verified actors spawned.
         *  Called by Papyrus only when at least one actor was successfully created. */
        void ConfirmManifestationCooldown();

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
        int GetWarDeclarationCooldownDays() const { return warDeclarationCooldownDays_.load(); }
        int GetMoraleDecayPerTick() const { return moraleDecayPerTick_.load(); }

        void SetEnabled(bool val) { enabled_.store(val); }
        void SetTickIntervalHours(int val) { tickIntervalHours_.store(std::clamp(val, 1, 24)); }

        /** Reload politics settings from settings.yaml. */
        void LoadSettings();

        // =================================================================
        // War Lifecycle
        // =================================================================

        /** Declare war between two factions.
         *  Validates: both factions exist, no active war between them, max wars not exceeded,
         *  cooldown not active. Creates faction_wars row and records war_declaration event.
         *  Returns war ID or -1 on failure. */
        int DeclareWar(const std::string& factionA, const std::string& factionB, float gameTime);

        /** Process one war tick for all active wars.
         *  Applies morale decay, checks surrender conditions.
         *  Returns JSON string with war updates for Papyrus to dispatch as events. */
        std::string ProcessWarTick(float gameTime);

        /** End a specific war with a victor. Records surrender event.
         *  Returns true on success. */
        bool EndWar(const std::string& factionA, const std::string& factionB,
                    const std::string& victor, float gameTime);

        /** Get the number of currently active wars. */
        int GetActiveWarCount();

        /** Get the DB war ID for an active war between two factions. Returns -1 if no war. */
        int GetActiveWarId(const std::string& factionA, const std::string& factionB);

        /** Get war strength for a faction in an active war. Returns 0 if no war. */
        int GetWarStrength(const std::string& factionA, const std::string& factionB, const std::string& queryFaction);

        /** Get war morale for a faction in an active war. Returns -1 if no war. */
        int GetWarMorale(const std::string& factionA, const std::string& factionB, const std::string& queryFaction);

        /** Record an off-screen battle result, applying morale/strength changes.
         *  Returns battle ID or -1 on failure. */
        int RecordOffScreenBattle(const std::string& factionA, const std::string& factionB,
                                  const std::string& location, const std::string& result,
                                  const std::string& narrative, int attackerLosses, int defenderLosses,
                                  const std::string& victor);

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

        /** Get default factions.yaml content. Written to disk if file doesn't exist. */
        static std::string GetDefaultFactionConfig();

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
        std::atomic<int> warDeclarationCooldownDays_{7};
        std::atomic<int> moraleDecayPerTick_{2};

        std::atomic<bool> initialized_{false};

        // Manifestation cooldown — last game time a political event was physically spawned
        std::atomic<float> lastManifestationTime_{0.0f};
        static constexpr float MANIFESTATION_COOLDOWN_HOURS = 4.0f;
        static constexpr float NEARBY_LEADER_DISTANCE = 5000.0f;

        // Crime gold baseline — last-known crime gold per faction (factionId → gold)
        // Used to detect NEW crime gold increases since last check
        std::unordered_map<std::string, int> crimeGoldBaseline_;
    };

}  // namespace IntelEngine
