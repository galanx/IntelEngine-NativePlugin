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
         * Build a compact bio line for DM context: race + notable factions.
         * Filters out internal/crime factions. Returns e.g., "Nord | Companions, Stormcloaks"
         */
        static std::string GetNPCBioLine(RE::Actor* actor);

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

        /**
         * Record that the LLM picked a story type. Volatile (per session).
         * Used to build type count stats for DM prompt balancing.
         */
        void NotifyStoryTypePicked(const std::string& storyType);

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

        /**
         * Get FormIDs of all NPCs in the last DM candidate pool.
         * Used by Papyrus to pre-warm cooldowns from StorageUtil before the DM call.
         */
        std::vector<RE::FormID> GetDMCandidatePoolFormIDs() const;

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

    private:
        NPCIndex() = default;
        ~NPCIndex() = default;
        NPCIndex(const NPCIndex&) = delete;
        NPCIndex& operator=(const NPCIndex&) = delete;

        // Index loaded NPC (used during BuildIndex - includes location tracking)
        void IndexLoadedNPC(RE::Actor* actor);

        // Cached lookup of IntelEngine_StoryEngineCooldown global (default 24h)
        static float GetStoryCooldownHours();

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

        // Story type pick counts (volatile per session, for DM prompt balancing)
        std::unordered_map<std::string, int> m_storyTypeCounts;

        bool m_indexBuilt = false;
    };

}  // namespace IntelEngine
