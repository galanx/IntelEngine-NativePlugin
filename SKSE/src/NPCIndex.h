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

#include <unordered_map>
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

        bool m_indexBuilt = false;
    };

}  // namespace IntelEngine
