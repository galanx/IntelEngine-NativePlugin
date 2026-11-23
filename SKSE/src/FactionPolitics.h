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

#include <set>
#include "Ranks.h"
#include <nlohmann/json.hpp>
#include <mutex>
#include <optional>
#include <algorithm>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace SKSE {
    class SerializationInterface;
}

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
        int peSide = -1;            // optional Political Engine side (0-3); -1 = derive from skyrimFactionId
        // The quests of the game that must be finished before this faction is free to go to war or be harmed
        // ("free_after" in factions.yaml; "A|B": either). Not given: the game's own for its skyrim_faction_id
        // (VanillaWar::QuestlineOf). Given and empty: never bound.
        std::vector<std::string> freeAfter;
        bool freeAfterSet = false;
    };

    /** Political Engine side of an organization: its pe_side if set, else its engine-faction default (-1 = none). */
    inline int OrganizationPESide(const FactionConfig& f) {
        if (f.peSide >= 0) return f.peSide;
        if (f.skyrimFactionId == "CWImperialFaction") return 1;
        if (f.skyrimFactionId == "CWSonsFaction") return 2;
        if (f.skyrimFactionId == "ThalmorFaction") return 3;
        return -1;
    }

    /** Allies/rivals of a realm on PE side `side`, from the organizations' sides: same side = ally; Stormcloaks vs Empire or Aldmeri = rival; Empire/Aldmeri = neither; Independent (0) = none. */
    inline std::pair<std::vector<std::string>, std::vector<std::string>> PESideRelations(int side, const std::vector<FactionConfig>& factions) {
        std::pair<std::vector<std::string>, std::vector<std::string>> out;
        if (side < 1 || side > 3) return out;
        for (const auto& f : factions) {
            const int s = OrganizationPESide(f);
            if (s == side) out.first.push_back(f.id);
            else if (s >= 1 && s <= 3 && s + side != 4) out.second.push_back(f.id);  // {1,3} (Empire/Aldmeri) is neither
        }
        return out;
    }

    // Co-save record of the leaders the game has shown to be dead for good.
    constexpr std::uint32_t kFallenLeadersRecord = 'IEFL';
    // Co-save record of a political event held back to be seen (FactionPolitics::HeldEvent).
    constexpr std::uint32_t kHeldEventRecord = 'IEHE';

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

        /** Where the user's faction file is (for the faction editor). */
        std::string ConfigPath() const { return GetConfigPath(); }
        /** The faction file was written anew (the faction editor): read it in again, with what start-up does after
         *  reading it (default relations for new pairs, scores, the state file). Any thread. */
        bool ReloadConfig();

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
                                 int delta, float gameTime, const std::string& locationName = "",
                                 const nlohmann::json& position = nullptr, const std::string& newsFrom = "");
        std::string GetFactionsJson();

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
        // Async Political DM Tick — split for off-main-thread context build.
        // =================================================================

        /** Per-leader snapshot used by the async politics tick.
         *  wasResolved: FindByName returned non-null on the main thread.
         *  confirmedDead: actor was resolved AND IsDead() AND !IsEssential() — used by
         *  the worker-safe FactionToJsonFromSnapshot to filter out permanently dead leaders
         *  without re-touching the engine. */
        struct PoliticsLeaderSnapshot {
            std::string name;
            std::string factionId;
            std::string hold;
            bool        nearby = false;
            RE::FormID  formId = 0;          // 0 if FindByName failed
            bool        wasResolved = false; // FindByName returned a valid actor
            bool        confirmedDead = false;
        };

        /** Tick-level snapshot for the politics tick. */
        struct PoliticsTickSnapshot {
            float       currentGameTime = 0.f;
            RE::FormID  playerFormId    = 0;
            std::string playerHold;
            std::vector<PoliticsLeaderSnapshot> leaders;
            nlohmann::json vanillaSides = nlohmann::json::array();
            std::uint8_t civilWar = 0;   // VanillaWar::State: the game's own civil war as the game keeps it
            int playerSide = 0;          // 0 none, 1 the Empire, 2 the Stormcloaks
            std::unordered_map<std::string, std::string> bound;   // faction id -> the story of the game that still binds it
        };

        /**
         * Phase A — main thread. Resolve faction leaders to actors, capture nearby
         * status, capture player formId + hold. No SQL; no markdown; just engine reads.
         */
        PoliticsTickSnapshot BuildPoliticsTickSnapshot(float currentGameTime);

        /**
         * Phase B — worker thread. Build the political DM JSON context using the
         * snapshot. All DB queries (relations, wars, standings, MemoryDB enrichment
         * per leader, recent player dialogue) run here.
         */
        std::string BuildPoliticalContextFromSnapshot(const PoliticsTickSnapshot& snap);

        /** Worker-safe FactionToJson variant — uses the snapshot's pre-resolved
         *  per-leader liveness instead of calling NPCIndex::FindByName + actor->IsDead.
         *  Returns empty json if all leaders are confirmed dead (faction disbanded). */
        nlohmann::json FactionToJsonFromSnapshot(const FactionConfig& f,
                                                 const PoliticsTickSnapshot& snap) const;

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
         *  Includes: non-neutral relations, recent events, active wars. Lightweight.
         *  Worker-thread-safe overload: caller passes currentGameTime to avoid
         *  RE::Calendar access from non-main threads. The no-arg version reads
         *  Calendar on the main thread and forwards. */
        std::string BuildPoliticalSummary();
        std::string BuildPoliticalSummary(float currentGameTime);

        /** Get the most recent witnessable political event (assassination, brawl,
         *  sabotage, espionage, border_skirmish) that happened within the current
         *  tick window. Returns empty string if none. Used by Story DM to hint
         *  at events the player could witness nearby.
         *  Worker-thread-safe overload: caller passes currentGameTime. */
        std::string GetLatestWitnessableEvent();
        std::string GetLatestWitnessableEvent(float currentGameTime);

        /** Check if a political event should physically manifest near the player.
         *  Compares player's hold against involved factions' holds.
         *  Returns JSON spawn instructions if player is at the event location,
         *  empty string if not. Guards: cooldown, no active battle, exterior only. */
        std::string CheckEventManifestation(const std::string& factionA,
                                            const std::string& factionB,
                                            const std::string& eventType);

        /** What is seen is not told before it has happened. An event that is to be shown near the player (an attempt
         *  on a leader's life, a skirmish) is held back here, unrecorded, until the script says how it went. */
        struct HeldEvent {
            bool set = false;
            std::string factionA, factionB, eventType, description;
            int delta = 0;
            float gameTime = 0.f;
        };
        /** It could not be shown after all: it is recorded and told as it was written (it happened where nobody of
         *  this tale was). */
        void ManifestationFailed();
        /** It was shown and has recorded itself (a skirmish does, like a battle): nothing more to record. */
        void ManifestationDone();
        /** How an attempt on a leader's life ended, as it was seen: "foiled" (the player killed the assassin before
         *  the blow), "saved" (after it), "failed" (somebody else did), "killed" (the leader is dead), "slain_by_player"
         *  (by the player's own hand), "escaped". Records it, tells it, moves the player's standing.
         *  Returns JSON: {notification, thanks, reward, targetFact, witnessFact} */
        std::string AssassinationOutcome(const std::string& targetFaction, const std::string& attackerFaction,
                                         const std::string& leaderName, const std::string& outcome, const std::string& place,
                                         const std::string& playerName, const std::string& hold);
        /** Nothing is held back any more (a game is loaded). */
        void ForgetHeldEvent();

        // ---- the fallen
        // A leader the game has shown to be dead for good is remembered as that, with the save. Whether a leader
        // lives was asked of the game each time, and of a body the game no longer keeps (its cell reset, the corpse
        // cleaned away) the answer is "nobody of that name": taken for alive. A faction that had lost its last
        // leader came back to the table when the bodies were gone.
        bool IsFallen(const std::string& factionId, const std::string& leaderName) const;
        /** Returns true when this is news. shownName: what the game calls them (told in the game's language). */
        bool MarkFallen(const std::string& factionId, const std::string& leaderName, const std::string& shownName);
        /** Tells of the leaders found dead since the last time. Returns how many. */
        int AnnounceFallen(float gameTime);
        /** Has this faction lost every one of its leaders? It does nothing any more, and nothing is done to it. */
        bool IsDisbanded(const std::string& factionId);
        void Save(SKSE::SerializationInterface* intfc) const;
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void ForgetFallen();
        /** A save with no record of the fallen (made before 4.0 kept them, a 3.8 save): every leader already dead in it
         *  is remembered as dead without a word (4.0 told each of them as fresh news, all at once, on the first look
         *  after the upgrade). Once a game is loaded, on the game's thread; does nothing when the record was there. */
        void NoteTheDeadQuietly();
        /** What is held back to be seen (HeldEvent) goes with the save, so that a load in the middle of the showing
         *  still tells it (Battle.OnGameReload: ManifestationFailed). 4.0 kept it in memory only and lost it. */
        void SaveHeld(SKSE::SerializationInterface* intfc);
        void LoadHeld(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);

        // ---- the ladder (Ranks.h): what the player has done for a faction, and the rank that earns them with it
        Ranks::Record PlayerRecord(const std::string& factionId) const;
        /** The rank the faction honours now (none while it takes the player for an enemy). */
        int PlayerRank(const std::string& factionId);
        /** What that rank is called with this faction ("Veteran", "Trusted Hand"); empty for no rank. */
        std::string RankTitle(const std::string& factionId, int rank) const;
        /** Something done for a faction. Returns the new rank when this lifted the player to it, else 0. */
        int EarnRenown(const std::string& factionId, Ranks::Deed deed, int times = 1);
        /** How the faction's own speak of the player: "a Veteran of the Imperial Legion: fought in 4 of their
         *  battles (3 won)". Empty when there is nothing to say. */
        std::string PlayerKnownAs(const std::string& factionId);
        /** For the author of a town's story: the times these people live in, a few lines: who is at war with whom,
         *  and how the factions take the player (a rank earned, a faction that holds them for an enemy or thinks well
         *  of them). Empty with politics off or nothing to say. Reads the political database: off the game's thread. */
        std::string TimesForTales(const std::string& player);
        /** Everything about the ladder, for the dashboard: [{faction, name, rank, title, renown, next, ...}] */
        nlohmann::json RanksJson();
        void SaveRanks(SKSE::SerializationInterface* intfc) const;
        void LoadRanks(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void ForgetRanks();

        /** Time passes between two factions that leave each other alone: old wounds close (a score below where the
         *  two usually stand climbs back to it), warmth nobody tends fades (one above it sinks back, never below
         *  nought). Once a game day, and only where nothing has happened between the two for three days and they are
         *  not at war. Without it a score that had reached -100 or +100 stayed there for good. Returns how many moved. */
        int DriftRelations(float gameTime);

        /** Confirm manifestation cooldown after Papyrus verified actors spawned.
         *  Called by Papyrus only when at least one actor was successfully created. */
        void ConfirmManifestationCooldown();

        /** Reset manifestation cooldown. Called by director dispatch to allow
         *  repeated testing without waiting 4 game hours. */
        void ResetManifestationCooldown();

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
        bool GetPEIntegration() const { return peIntegration_.load(); }
        bool SetPEIntegration(bool val);
        /** Sets each PE_<realm> faction's leader to the realm's current ruler ({"<realm_id>": actor form id}). */
        void SyncPERulers(const nlohmann::json& rulers);
        /** Sets each PE_<realm> faction's allies/rivals from its live PE side ({"<realm_id>": side}); PoliticalDB relation scores are untouched. */
        void SyncPESides(const nlohmann::json& sides);
        bool GetPEReactionsMoveRelations() const { return peReactionsMoveRelations_.load(); }
        bool SetPEReactionsMoveRelations(bool val);

        bool SetEnabled(bool val);
        bool SetTickIntervalHours(int val);
        bool SetMaxRelationChangePerTick(int val);
        bool SetMaxActiveWars(int val);

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

        /** fix 94: may this pair ever be at war? (war_allowed in factions.yaml; PE_ realms never). Takes configMutex_. */
        bool WarPossible(const FactionConfig& a, const FactionConfig& b) const;
        /** WarPossible for a caller that already holds configMutex_. */
        bool WarPossibleLocked(const FactionConfig& a, const FactionConfig& b) const;

        /** fix 94: declare the lowest-scored eligible Critical pair (Rule 3) in code. Returns war id or -1. */
        int EnforceWarRule(const PoliticsTickSnapshot& snap);
        /** Main thread, each political tick before the war rule: the simulation is held to the game's own civil war
         *  (VanillaWar.h): no war between the Empire and the Stormcloaks goes on under the truce or after the game
         *  decided it, the Empire and the Thalmor stay short of war while the Concordat binds them, and the Empire and
         *  the Stormcloaks never drift into friendship. */
        void HoldToTheGamesWar(const PoliticsTickSnapshot& snap);
        /** The id of a faction as factions.yaml writes it, whatever case it was asked for in ("CollegeOfWinterholdFaction"
         *  for "CollegeofWinterholdFaction"); empty when there is no such faction. */
        std::string CanonicalId(const std::string& anyCase) const;
        /** The game's own civil war as last read (VanillaWar::State). Any thread. */
        std::uint8_t CivilWarState() const { return vanillaWar_.load(); }
        /** The story of the game that still binds a faction (the quest's name), empty when it is free. Any thread. */
        std::string BoundBy(const std::string& factionId) const;
        int CivilWarPlayerSide() const { return vanillaAllegiance_.load(); }

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
                                  const std::string& victor,
                                  const nlohmann::json* physicalPosition = nullptr, const std::string& newsFrom = "");

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

        /** Sync vanilla faction membership with political standing.
         *  If player is in a vanilla faction (via skyrim_faction_id) but has 0 standing,
         *  grants initial standing of 40 (friendly threshold). Called on each political tick. */
        int SyncVanillaFactionStandings();

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
        // The leaders and the hold as the game shows them (a translated game), added beside the configured names.
        static void ShownAsInGame(nlohmann::json& fj, const FactionConfig& f);

        /** Build faction id→name lookup map (acquires configMutex_ internally). */
        std::unordered_map<std::string, std::string> BuildIdToNameMap() const;

        // Config data (protected by mutex for hot-reload)
        mutable std::mutex configMutex_;
        std::vector<FactionConfig> factions_;
        std::vector<DefaultRelation> defaultRelations_;
        std::vector<std::pair<std::string, std::vector<std::string>>> warAllowed_;  // fix 94; under configMutex_
        uint64_t factionRevision_ = 0;
        std::string revisionLoadId_;
        std::unordered_map<std::string, bool> disbandedSnapshot_;
        std::unordered_map<std::string, size_t> factionIndex_;  // id -> index in factions_
        std::unordered_map<std::string, std::string> nameToId_; // lowercase name -> id

        // Settings (atomic for lock-free reads from Papyrus thread)
        std::atomic<bool> enabled_{true};
        std::atomic<int> tickIntervalHours_{6};
        std::atomic<int> maxRelationChangePerTick_{15};
        std::atomic<int> maxActiveWars_{2};
        std::atomic<int> warDeclarationCooldownDays_{7};
        std::atomic<int> moraleDecayPerTick_{2};
        // Off unless the player switches it on (settings.yaml `politics.pe_integration`, the MCM, the dashboard
        // or SkyrimNet's settings page): another mod's campaign should not start feeding IntelEngine's
        // politics just because both are installed.
        std::atomic<bool> peIntegration_{false};
        std::atomic<bool> peReactionsMoveRelations_{true};
        std::mutex peMutex_;
        std::mutex heldMutex_;
        HeldEvent held_;
        mutable std::mutex fallenMutex_;
        std::set<std::string> fallen_;                                      // "faction|leader's name as configured"
        std::vector<std::pair<std::string, std::string>> fallenToTell_;     // faction, name as the game shows it
        std::atomic<bool> fallenQuiet_{false};   // a game loaded without a record of the fallen: the dead found now are not news
        std::atomic<float> lastDriftTime_{0.0f};
        mutable std::mutex ranksMutex_;
        Ranks::Book ranks_;
        nlohmann::json pendingPE_ = nlohmann::json::object(), eligiblePE_ = nlohmann::json::object();
        std::string peLoadId_;

        std::atomic<bool> initialized_{false};

        // Manifestation cooldown — last game time a political event was physically spawned
        std::atomic<float> lastManifestationTime_{0.0f};
        static constexpr float MANIFESTATION_COOLDOWN_HOURS = 4.0f;
        static constexpr float NEARBY_LEADER_DISTANCE = 5000.0f;

        // Crime gold baseline — last-known crime gold per faction (factionId → gold)
        // Used to detect NEW crime gold increases since last check
        std::unordered_map<std::string, int> crimeGoldBaseline_;

        // Tracks which factions have been synced from vanilla membership this session.
        // Prevents repeated boosting if the player later loses standing through gameplay.
        // Cleared on Initialize() (new game / load).
        std::unordered_set<std::string> vanillaSynced_;
        std::unordered_map<std::string, std::string> idByLower_;   // lowercase id -> id as written (ids are asked for in any case)
        std::atomic<std::uint8_t> vanillaWar_{0};                  // VanillaWar::State, read on the game's thread each tick
        std::atomic<int> vanillaAllegiance_{0};
        mutable std::mutex boundMutex_;
        std::unordered_map<std::string, std::string> boundBy_;   // read on the game's thread each tick (BuildPoliticsTickSnapshot)
        /** What happened in the world's politics becomes talk (RumorBridge): how far it travels follows its kind. */
        void News(const std::string& factionA, const std::string& eventType, const std::string& text, int eventId,
                  const std::string& from = "");
    };

}  // namespace IntelEngine
