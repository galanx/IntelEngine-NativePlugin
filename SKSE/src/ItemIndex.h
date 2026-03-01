#pragma once

/**
 * Item Index - Valuable Item Discovery System
 *
 * Maintains an indexed database of all valuable items for quest reward spawning.
 * Built from game data at startup - indexes weapons, armor, misc items, and spell tomes.
 *
 * Uses:
 * - Hash-based exact match (O(1))
 * - Levenshtein distance for typo tolerance
 * - Random selection from valuable item pool
 */

#include "Plugin.h"
#include "StringUtils.h"

#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>

namespace IntelEngine {

    class ItemIndex {
    public:
        static ItemIndex* GetSingleton() {
            static ItemIndex instance;
            return &instance;
        }

        struct ItemEntry {
            RE::FormID formID = 0;
            std::string name;
            int goldValue = 0;
            RE::FormType formType = RE::FormType::None;
        };

        /**
         * Build the item index from all loaded forms.
         * Called on data loaded event.
         */
        void BuildIndex();

        /**
         * Check if the index has been built.
         */
        bool IsIndexBuilt() const { return m_indexBuilt; }

        /**
         * Find an item by name (exact -> fuzzy -> substring).
         * Returns the TESBoundObject or nullptr.
         */
        RE::TESBoundObject* FindByName(const std::string& searchTerm);

        /**
         * Check if an item with this name exists in the index.
         */
        bool ValidateName(const std::string& name);

        /**
         * Get a random valuable item above a minimum gold value.
         * Returns empty string if no items qualify.
         */
        std::string GetRandomValuableName(int minGoldValue);

        /**
         * Get a random valuable item, excluding recently used items.
         * Falls back to unrestricted selection if all candidates are excluded.
         */
        std::string GetRandomValuableName(int minGoldValue,
            const std::unordered_set<std::string>& excludeNames);

        /**
         * Get the total number of indexed items.
         */
        size_t GetItemCount() const;

    private:
        ItemIndex() = default;
        ~ItemIndex() = default;
        ItemIndex(const ItemIndex&) = delete;
        ItemIndex& operator=(const ItemIndex&) = delete;

        // Thread-safe access
        mutable std::shared_mutex m_mutex;

        // Main index: lowercase name -> FormID
        std::unordered_map<std::string, RE::FormID> m_itemsByName;

        // All indexed names for fuzzy search
        std::vector<std::string> m_allNames;

        // Pre-filtered valuable items for random selection
        std::vector<ItemEntry> m_valuableItems;

        bool m_indexBuilt = false;
    };

}  // namespace IntelEngine
