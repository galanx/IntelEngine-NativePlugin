#pragma once

/**
 * NPC Index - Fuzzy Search System
 *
 * Maintains an indexed database of all NPCs for fast fuzzy name matching.
 * Built from game data at startup - no external database dependencies.
 *
 * Uses:
 * - Hash-based exact match (O(1))
 * - Levenshtein distance for typo tolerance
 * - Partial substring matching
 */

#include "Plugin.h"
#include "StringUtils.h"
#include "SlotTracker.h"
#include "MemoryDB.h"  // RankedCandidate (used by StoryDMPhaseAPrefetch)

#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

namespace IntelEngine {

    class NPCIndex {
    public:
        static NPCIndex* GetSingleton() {
            static NPCIndex instance;
            return &instance;
        }

        /**
         * Build the NPC index from all loaded actors.
         * Called on data loaded event.
         */
        void BuildIndex();

        /**
         * Refresh the index to catch runtime changes.
         * Called on game load.
         */
        void RefreshIndex();

        /**
         * Rebuild the index from scratch.
         * Called via Papyrus API.
         */
        void RebuildIndex();

        /**
         * Check if index has been built.
         */
        bool IsIndexBuilt() const { return m_indexBuilt; }

        /**
         * Find an NPC by name using fuzzy matching.
         *
         * Search order:
         * 1. Exact match (case-insensitive)
         * 2. Levenshtein fuzzy match (threshold: 3)
         * 3. Partial substring match
         *
         * @param searchTerm Name to search for
         * @return Actor pointer if found, nullptr otherwise
         */
        RE::Actor* FindByName(const std::string& searchTerm);

        /**
         * Proximity-aware NPC search.
         * Same fuzzy matching as FindByName, but when multiple NPCs match,
         * returns the one closest to nearActor.
         * Falls back to FindByName if nearActor is nullptr.
         *
         * @param searchTerm Name to search for
         * @param nearActor Actor to measure distance from
         * @return Closest matching Actor, or nullptr
         */
        RE::Actor* FindByNameNear(const std::string& searchTerm, RE::Actor* nearActor, bool allowSelf = false);

        /**
         * Check if an NPC is accessible (not disabled, in loaded cell).
         *
         * @param actor Actor to check
         * @return True if accessible
         */
        bool IsAccessible(RE::Actor* actor);

        /**
         * Get a name suggestion for a failed search.
         *
         * @param searchTerm The failed search term
         * @return Closest matching name, or empty string
         */
        RE::BSFixedString GetSuggestion(const std::string& searchTerm);

        /**
         * Get index statistics.
         */
        size_t GetNPCCount() const { return m_allNames.size(); }
        size_t GetLoadedCount() const { return m_npcIndex.size(); }

        /**
         * Find NPC FormID by name (works for unloaded NPCs too).
         * This allows finding NPCs anywhere in the world.
         *
         * @param searchTerm Name to search for
         * @return FormID if found, 0 otherwise
         */
        RE::FormID FindFormIdByName(const std::string& searchTerm);

        /**
         * Get current location of an NPC (if known).
         *
         * @param searchTerm NPC name
         * @return Location name or empty string
         */
        RE::BSFixedString GetNPCLocation(const std::string& searchTerm);

        /**
         * Get Actor from FormID (resolving unloaded NPCs).
         * May return nullptr if NPC is in unloaded cell.
         *
         * @param formId NPC FormID
         * @return Actor pointer or nullptr
         */
        RE::Actor* GetActorFromFormId(RE::FormID formId);

        /**
         * Resolve a MemoryDB candidate to a live Actor*.
         * Tries FormID lookup first (fast path), falls back to fuzzy name search.
         *
         * BUG WORKAROUND: SkyrimNet's uuid_mappings table stores FormIDs from the
         * session when the NPC was first registered. Mod updates, load-order changes,
         * or base-form vs reference mismatches can cause these stored FormIDs to become
         * stale (e.g., Heidi: DB=0xBC027633, Game=0xBC018519). Name-based fallback via
         * FindByName() ensures we still find the NPC using the reliable display name.
         *
         * @param formId FormID from MemoryDB (may be stale)
         * @param name Actor display name from uuid_mappings.actor_name (reliable)
         * @return Actor pointer or nullptr if unresolvable
         */
        RE::Actor* ResolveFromMemoryDB(RE::FormID formId, const std::string& name);

        /**
         * Get a random eligible NPC for the Story Engine.
         * Filters: alive, not disabled, not in combat, not in player's cell,
         * not already on an IntelEngine task, not on cooldown.
         *
         * @return Random eligible Actor, or nullptr if no candidates
         */
        RE::Actor* GetRandomStoryCandidate();

        /**
         * Get a story candidate ranked by MemoryDB story engagement.
         * Falls back to GetRandomStoryCandidate if no memory-ranked candidates available.
         */
        RE::Actor* GetMemoryDrivenCandidate();

        /**
         * Get a story candidate related to the given actor (shared event history).
         * Falls back to any ranked NPC excluding relatedTo.
         */
        RE::Actor* GetRelatedCandidate(RE::Actor* relatedTo);

        /**
         * Find a suitable messenger to deliver a message on behalf of sender.
         * Cascade: household → social associate → same-hold guard → any civilian.
         * Returns nullptr if no messenger found (caller decides self-delivery vs reject).
         */
        RE::Actor* FindMessengerForSender(RE::Actor* sender);

        /**
         * Shared eligibility filter for all story candidate selection methods.
         * Strict version: requires actor to be in a loaded cell.
         */
        static bool IsEligibleStoryCandidate(RE::Actor* actor, RE::Actor* player,
            RE::TESObjectCELL* playerCell, SlotTracker* tracker);

        /**
         * Relaxed eligibility filter for MemoryDB-ranked candidates.
         * Allows unloaded NPCs (no GetParentCell() requirement).
         * Safe checks only: dead, disabled, active task, cooldown, keyword.
         */
        static bool IsEligibleStoryCandidateRelaxed(RE::Actor* actor, RE::Actor* player,
            SlotTracker* tracker);

        /**
         * Classify an NPC's archetype based on class, factions, and combat capability.
         * Returns: "WARRIOR", "MAGE", "ROGUE", "PRIEST", "NOBLE", "BARD", "CIVILIAN"
         */
        static std::string ClassifyNPCArchetype(RE::Actor* actor);

        /**
         * Check if an NPC is a Jarl (has JobJarlFaction).
         */
        static bool IsJarl(RE::Actor* actor);

        /**
         * Check if an NPC holds a high-status position (Jarl, steward, court wizard, housecarl).
         * These NPCs should never travel personally — they send couriers/messengers.
         */
        static bool IsHighStatus(RE::Actor* actor);

        /**
         * Build comma-separated list of eligible story types for a candidate.
         * Considers archetype, high-status role, environment (interior/danger).
         */
        static std::string GetEligibleStoryTypes(RE::Actor* actor,
            const std::string& archetype, bool dangerous, bool interior);

        /**
         * Set danger zone dispatch policy (synced from MCM via Papyrus).
         * 0=allow all, 1=block civilians, 2=followers only, 3=block all
         */
        void SetDangerZonePolicy(int policy);
        int GetDangerZonePolicy() const { return m_dangerZonePolicy.load(std::memory_order_relaxed); }

        /**
         * Set player home visit policy (synced from MCM via Papyrus).
         * 0=allow all, 1=block civilians, 2=followers only, 3=block all
         */
        void SetPlayerHomePolicy(int policy);
        int GetPlayerHomePolicy() const { return m_playerHomePolicy.load(std::memory_order_relaxed); }

        /**
         * Set per-story-type hold restriction policy (synced from MCM via Papyrus).
         * 0=no restriction, 1=same hold civilians only, 2=same hold except followers, 3=same hold everyone
         * @param storyType One of: seek_player, informant, road_encounter, ambush, stalker, message, quest
         * @param policy Hold restriction level (0-3)
         */
        void SetHoldRestrictionPolicy(const std::string& storyType, int policy);

        /**
         * Get hold restriction policy for a story type.
         */
        int GetHoldRestrictionPolicy(const std::string& storyType) const;

        /**
         * Check if an NPC passes the hold restriction policy.
         * @param actor NPC to check
         * @param playerHold Player's current hold name
         * @param policy Hold restriction level (0-3)
         * @return true if the NPC is allowed to be dispatched
         */
        static bool PassesHoldRestriction(RE::Actor* actor, const std::string& playerHold, int policy);

        /**
         * Check if actor is in PotentialFollowerFaction (can be recruited as follower).
         */
        static bool IsPotentialFollower(RE::Actor* actor);

        /**
         * Check if the player is in a location on the blocklist.
         * Uses plugin config API with 30-second cache.
         */
        static bool IsPlayerInBlockedLocation();
        static bool IsPlayerInWhitelistedLocation();

        /**
         * Build a compact bio line for DM context: race + notable factions.
         * Filters out internal/crime factions. Returns e.g., "Nord | Companions, Stormcloaks"
         */
        static std::string GetNPCBioLine(RE::Actor* actor);

        // Engine-touch fallback only (no bio file I/O). Use in Phase A snapshots
        // so the slow file read can move to Phase B (worker thread).
        static std::string GetNPCBioFallbackLine(RE::Actor* actor);

        /**
         * Resolve a name from the DM response to the exact Actor from the last candidate pool.
         * Uses stored FormIDs from BuildDungeonMasterContext/BuildNPCInteractionContext,
         * ensuring we get the EXACT NPC that was in the pool (not a different NPC with the same name).
         * Falls back to FindByName if the name isn't in the pool.
         */
        RE::Actor* ResolveStoryCandidate(const std::string& name);

        /**
         * Record that an NPC was picked by the story engine at the given game time.
         * Called from Papyrus when ApplyCooldownCheck succeeds.
         */
        void NotifyStoryCooldown(RE::FormID formId, float gameTime);

        /**
         * Check if an NPC is on story cooldown (short hard block).
         * @param cooldownHours Hard block duration in game hours
         */
        bool IsOnStoryCooldown(RE::FormID formId, float cooldownHours) const;

        /** Record that an NPC was picked for a social interaction at the given game time. */
        void NotifySocialCooldown(RE::FormID formId, float gameTime, float cooldownHours);

        /** Check if an NPC is on social cooldown. Uses last reported cooldown hours. */
        bool IsOnSocialCooldown(RE::FormID formId) const;

        /** Get the social cooldown hours (last reported from Papyrus MCM). */
        float GetSocialCooldownHours() const;

        /**
         * Record that the LLM picked a story type. Volatile (per session).
         * Used to build type count stats for DM prompt balancing.
         */
        void NotifyStoryTypePicked(const std::string& storyType);

        /** Set recent gossip context for the Story DM prompt.
         *  Injects hold names by resolving the first NPC name in each line. */
        void SetRecentGossipContext(const std::string& gossipLines);

        /** Get game time of last successful story dispatch (0 = never). */
        float GetLastDispatchGameTime() const {
            std::unique_lock lock(m_mutex);
            return m_lastStoryDispatchGameTime;
        }
        std::string GetPreferredNPCType() const;

        /**
         * Record a quest item that was used in a find_item quest.
         * Tracks last N items for rotation (prevents repeats in DM prompt + fallback).
         */
        void NotifyQuestItemUsed(const std::string& itemName);

        /**
         * Record an NPC that was used as a rescue victim.
         * Tracks last N victims for rotation (prevents repeats in DM prompt).
         */
        void NotifyRescueVictimUsed(const std::string& victimName);

        /**
         * Record a quest location that was used.
         * Tracks last N locations for rotation (prevents repeats in DM prompt).
         */
        void NotifyQuestLocationUsed(const std::string& locationName);

        /**
         * Get recent quest items as comma-separated string for DM context.
         */
        std::string GetRecentQuestItemsString() const;

        /**
         * Get recent rescue victims as comma-separated string for DM context.
         */
        std::string GetRecentRescueVictimsString() const;

        /**
         * Get recent quest locations as comma-separated string for DM context.
         */
        std::string GetRecentQuestLocationsString() const;

        /**
         * Get recent quest item names as a set (for fallback exclusion).
         */
        std::unordered_set<std::string> GetRecentQuestItemNames() const;

        /**
         * Get household member names for a candidate NPC.
         * Returns comma-separated "Name (relationship)" string.
         * Uses LocationResolver's home index (bed-ownership based).
         */
        static std::string GetHouseholdString(RE::Actor* actor);

        /**
         * Build markdown snippet showing story type pick counts.
         * If relevantTypes is non-empty, only those types are included.
         */
        std::string GetStoryTypeCountsMarkdown(const std::unordered_set<std::string>& relevantTypes = {}) const;

        /**
         * Build the complete Dungeon Master context for the Story Engine.
         * Returns a JSON-escaped markdown string containing world state + candidate pool.
         * Papyrus embeds this as a single context variable in the DM prompt.
         *
         * @param maxCandidates Maximum number of candidates to include in the pool
         * @param absenceDays Minimum days since player interaction for eligibility
         * @return Pre-escaped context string, or empty if no eligible candidates
         */
        std::string BuildDungeonMasterContext(int maxCandidates, float absenceDays);

        /**
         * Build NPC-to-NPC interaction context for the NPC Social tick.
         * Groups eligible loaded NPCs by location, scores groups by density
         * and MemoryDB social history. Returns JSON-escaped markdown.
         *
         * @param maxPairs Maximum number of location groups to include
         * @return Pre-escaped context string, or empty if no eligible groups
         */
        std::string BuildNPCInteractionContext(int maxPairs);

        // -----------------------------------------------------------------
        // Async tick infrastructure (no engine touches in Phase B/markdown).
        // -----------------------------------------------------------------

        /** Per-actor data captured by Phase A (main thread). All fields are values. */
        struct NPCTickActorSnapshot {
            RE::FormID  formId        = 0;
            RE::FormID  cellFormId    = 0;
            std::string nameDisplay;
            std::string nameLower;
            std::string location;     // pre-resolved via LocationResolver
            std::string archetype;    // pre-classified
            std::string bio;          // pre-fetched
            bool        isFemale      = false;
            bool        isFollower    = false;
        };

        /** Tick-level snapshot for the NPC interaction tick (NPC DM). */
        struct NPCTickSnapshot {
            int               maxPairs           = 4;
            float             currentGameTime    = 0.f;
            RE::FormID        playerCellFormId   = 0;
            std::string       playerName;
            std::string       playerLocation;
            std::string       timeOfDayString;
            std::vector<NPCTickActorSnapshot> candidates;
        };

        /**
         * Phase A — main thread. Capture engine state for the NPC interaction tick.
         * Iterates loaded actors, applies all engine-touching eligibility filters
         * (alive, not in combat/hostile, NPC keyword, not follower, not on cooldown),
         * pre-resolves location/archetype/bio, returns a value-only snapshot.
         */
        NPCTickSnapshot BuildNPCTickSnapshot(int maxPairs);

        /**
         * Phase B — worker thread. Build markdown context from the snapshot.
         * Performs SQL queries (GetSociallyActiveFormIDs, GetFormattedMemories) and
         * formats the output. Updates m_npcCandidatePool under m_mutex.
         */
        std::string BuildNPCInteractionContextFromSnapshot(const NPCTickSnapshot& snap);

        /** Per-actor data for Story DM tick (richer than NPC tick — has position + dbFormId). */
        struct StoryDMActorSnapshot {
            RE::FormID  formId        = 0;
            RE::FormID  dbFormId      = 0;     // FormID used for MemoryDB queries (may be stale-but-valid)
            std::string nameDisplay;
            std::string nameLower;
            std::string location;
            std::string archetype;
            std::string bio;
            std::string npcHold;       // settlement (city/town) or hold fallback
            std::string factionName;   // pre-resolved political faction display name (empty if none)
            bool        isFemale      = false;
            bool        is3DLoaded    = false;
            float       posX          = 0.f;
            float       posY          = 0.f;
            float       dbScore       = 0.f;   // initial MemoryDB ranking score (0 for randoms/locmates)
            bool        fromMemoryDB  = false;
        };

        /** Tick-level snapshot for the Story DM tick. */
        struct StoryDMTickSnapshot {
            int               maxCandidates           = 7;
            float             absenceDays             = 3.0f;
            float             currentGameTime         = 0.f;
            float             currentDBHours          = 0.f;
            std::string       playerName;
            std::string       playerLocation;
            std::string       playerHold;
            bool              playerInteriorCell      = false;
            bool              playerInDangerousLocation = false;
            bool              playerAtInn             = false;
            float             playerX                 = 0.f;
            float             playerY                 = 0.f;
            std::string       timeOfDayString;
            float             lastStoryDispatchGameTime = 0.f;
            std::string       lastStoryDispatchType;
            int               memoriesPerCandidate    = 2;   // snapshotted from Settings on main thread
            std::vector<StoryDMActorSnapshot> candidates;
        };

        /** Worker-safe pre-fetch: results of the two SQL queries Story DM Phase A
         *  used to make synchronously. Done on the worker thread before Phase A so
         *  the main thread doesn't pay the cross-DLL SQLite cost. */
        struct StoryDMPhaseAPrefetch {
            std::unordered_set<std::string> recentPlayerNPCs;  // GetPlayerContext SQL
            std::vector<RankedCandidate>    ranked;            // GetActorEngagement SQL
        };

        /** Phase 0 — worker thread. Fetches the SQL inputs Phase A needs. */
        StoryDMPhaseAPrefetch FetchStoryDMPhaseAPrefetch(int maxCandidates, float absenceDays);

        /**
         * Phase A — main thread. Capture engine state for the Story DM tick.
         * Runs the same 3-pass actor scan as BuildDungeonMasterContext (MemoryDB-ranked +
         * random encounters + location-mates). The two SQL inputs (recentPlayerNPCs,
         * ranked) are pre-fetched on the worker thread by FetchStoryDMPhaseAPrefetch
         * so this function does only engine-touch work. Returns empty candidates
         * if player is in a blocked location.
         */
        StoryDMTickSnapshot BuildStoryDMTickSnapshot(int maxCandidates, float absenceDays,
                                                     const StoryDMPhaseAPrefetch& prefetch);

        /**
         * Phase B — worker thread. Score candidates, sort, trim, build markdown.
         * All per-candidate SQL queries run here (memories, dialogue, events, related,
         * bio relationships). Updates m_dmCandidatePool under m_mutex.
         */
        std::string BuildDungeonMasterContextFromSnapshot(const StoryDMTickSnapshot& snap);

        /**
         * Get FormIDs of all NPCs in the last DM candidate pool.
         * Used by Papyrus to pre-warm cooldowns from StorageUtil before the DM call.
         */
        std::vector<RE::FormID> GetDMCandidatePoolFormIDs() const;
        std::vector<RE::FormID> GetNPCCandidatePoolFormIDs() const;

        /** Scan loaded actors for those running one of the given packages. Returns JSON array. */
        std::string ScanActorsWithPackages(const std::vector<RE::FormID>& packageFormIDs);

        /**
         * Get location name for an NPC, with fallback for unloaded actors.
         * Loaded: uses current cell/location. Unloaded: uses editor location.
         */
        static std::string GetNPCLocationName(RE::Actor* actor);

        /**
         * Get hold name for an NPC, with fallback for unloaded actors.
         * Loaded: walks parent cell location chain. Unloaded: walks editor location chain.
         */
        static std::string GetNPCHoldName(RE::Actor* actor);

        /**
         * Get the settlement name (city/town) for an actor.
         * Walks up the BGSLocation hierarchy looking for LocTypeCity or LocTypeTown.
         * Falls back to immediate location name for wilderness NPCs.
         */
        static std::string GetActorSettlementName(RE::Actor* actor);

        // Cached lookup of IntelEngine_StoryEngineCooldown global (default 24h)
        // Returns max(MCM cooldown, absence days * 24) to ensure dispatched NPCs
        // stay out of the pool for at least the absence period.
        static float GetStoryCooldownHours();

    private:
        NPCIndex() = default;
        ~NPCIndex() = default;
        NPCIndex(const NPCIndex&) = delete;
        NPCIndex& operator=(const NPCIndex&) = delete;

        // Index loaded NPC (used during BuildIndex - includes location tracking)
        void IndexLoadedNPC(RE::Actor* actor);

        // Thread-safe access
        mutable std::shared_mutex m_mutex;

        // Main index: lowercase name -> Actor* (only loaded NPCs)
        std::unordered_map<std::string, RE::Actor*> m_npcIndex;

        // FormID index: lowercase name -> FormID (ALL NPCs including unloaded)
        std::unordered_map<std::string, RE::FormID> m_npcFormIds;

        // Current locations: lowercase name -> location name
        std::unordered_map<std::string, std::string> m_npcCurrentLocations;

        // All indexed names for fuzzy search
        std::vector<std::string> m_allNames;

        // Candidate pools for exact name->FormID resolution (no name ambiguity).
        // Separate pools per tick — each tick clears and rebuilds its own pool,
        // so an async LLM response always finds its candidates intact.
        // ResolveStoryCandidate checks DM pool first, then NPC pool.
        std::unordered_map<std::string, RE::FormID> m_dmCandidatePool;
        std::unordered_map<std::string, RE::FormID> m_npcCandidatePool;

        // Story cooldown mirror: FormID -> game time when last picked
        // Volatile (empty on load), self-heals after first tick
        std::unordered_map<RE::FormID, float> m_storyCooldowns;
        std::unordered_map<RE::FormID, float> m_socialCooldowns;
        std::atomic<float> m_socialCooldownHours{24.0f};  // updated from Papyrus MCM

        // Story type pick counts (volatile per session, for DM prompt balancing)
        std::unordered_map<std::string, int> m_storyTypeCounts;
        float m_lastStoryDispatchGameTime = 0.f;  // Story DM last dispatch time
        std::string m_lastStoryDispatchType;      // Story DM last dispatch type
        float m_lastNPCDispatchGameTime = 0.f;    // NPC DM last dispatch time
        std::string m_recentGossipContext;   // Pre-built gossip lines for DM context

        // Recent quest items FIFO (volatile per session, for rotation)
        static constexpr int MAX_RECENT_QUEST_ITEMS = 8;
        std::deque<std::string> m_recentQuestItems;

        // Recent rescue victims FIFO (volatile per session, for rotation)
        static constexpr int MAX_RECENT_RESCUE_VICTIMS = 6;
        std::deque<std::string> m_recentRescueVictims;

        // Recent quest locations FIFO (volatile per session, for rotation)
        static constexpr int MAX_RECENT_QUEST_LOCATIONS = 8;
        std::deque<std::string> m_recentQuestLocations;

        bool m_indexBuilt = false;

        // Danger zone dispatch policy (MCM-synced)
        // 0=allow all, 1=block civilians, 2=followers only, 3=block all
        std::atomic<int> m_dangerZonePolicy{1};

        // Player home visit policy (MCM-synced)
        // 0=allow all, 1=block civilians, 2=followers only, 3=block all
        std::atomic<int> m_playerHomePolicy{0};

        // Per-story-type hold restriction policies (MCM-synced)
        // 0=no restriction, 1=same hold civilians only, 2=same hold except followers, 3=same hold everyone
        // Protected by m_holdRestrictionMutex (writes rare, reads during candidate pool build)
        mutable std::mutex m_holdRestrictionMutex;
        std::unordered_map<std::string, int> m_holdRestrictionPolicies;
    };

}  // namespace IntelEngine
