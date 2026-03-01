/**
 * Item Index Implementation
 *
 * Builds and maintains an indexed database of valuable game items
 * for quest reward spawning (find_item quest sub-type).
 */

#include "ItemIndex.h"
#include <algorithm>
#include <random>

namespace IntelEngine {

    // Minimum gold value to qualify as "valuable" for random selection
    static constexpr int VALUABLE_THRESHOLD = 500;

    // Skip generic/common items that shouldn't appear as quest rewards
    static bool IsGenericItem(const char* name, int goldValue) {
        if (!name || name[0] == '\0') return true;
        if (goldValue <= 0) return true;

        // Skip items with very short names (usually placeholders)
        if (strlen(name) < 3) return true;

        return false;
    }

    // Check if a form is a playable item (not a template or internal form)
    static bool IsPlayableWeapon(RE::TESObjectWEAP* weapon) {
        if (!weapon) return false;
        if (weapon->IsDeleted()) return false;
        auto name = weapon->GetFullName();
        if (!name || name[0] == '\0') return false;
        return true;
    }

    static bool IsPlayableArmor(RE::TESObjectARMO* armor) {
        if (!armor) return false;
        if (armor->IsDeleted()) return false;
        auto name = armor->GetFullName();
        if (!name || name[0] == '\0') return false;
        return true;
    }

    void ItemIndex::BuildIndex() {
        std::unique_lock lock(m_mutex);

        logger::info("Building Item Index...");

        m_itemsByName.clear();
        m_allNames.clear();
        m_valuableItems.clear();

        auto* dh = RE::TESDataHandler::GetSingleton();
        if (!dh) {
            logger::error("[ItemIndex] TESDataHandler not available");
            return;
        }

        int totalIndexed = 0;
        int valuableCount = 0;

        // Phase 1: Index weapons
        for (auto* weapon : dh->GetFormArray<RE::TESObjectWEAP>()) {
            if (!IsPlayableWeapon(weapon)) continue;

            auto name = weapon->GetFullName();
            int goldValue = weapon->GetGoldValue();
            if (IsGenericItem(name, goldValue)) continue;

            std::string lowerName = StringUtils::ToLowerStd(name);

            // Prefer higher-value items when name collides
            auto existing = m_itemsByName.find(lowerName);
            if (existing != m_itemsByName.end()) {
                auto* existingForm = RE::TESForm::LookupByID(existing->second);
                if (existingForm && existingForm->GetGoldValue() >= goldValue) {
                    continue;  // Keep the higher-value one
                }
            }

            m_itemsByName[lowerName] = weapon->GetFormID();
            totalIndexed++;

            if (goldValue >= VALUABLE_THRESHOLD) {
                m_valuableItems.push_back({
                    weapon->GetFormID(),
                    std::string(name),
                    goldValue,
                    RE::FormType::Weapon
                });
                valuableCount++;
            }
        }

        // Phase 2: Index armor
        for (auto* armor : dh->GetFormArray<RE::TESObjectARMO>()) {
            if (!IsPlayableArmor(armor)) continue;

            auto name = armor->GetFullName();
            int goldValue = armor->GetGoldValue();
            if (IsGenericItem(name, goldValue)) continue;

            std::string lowerName = StringUtils::ToLowerStd(name);

            auto existing = m_itemsByName.find(lowerName);
            if (existing != m_itemsByName.end()) {
                auto* existingForm = RE::TESForm::LookupByID(existing->second);
                if (existingForm && existingForm->GetGoldValue() >= goldValue) {
                    continue;
                }
            }

            m_itemsByName[lowerName] = armor->GetFormID();
            totalIndexed++;

            if (goldValue >= VALUABLE_THRESHOLD) {
                m_valuableItems.push_back({
                    armor->GetFormID(),
                    std::string(name),
                    goldValue,
                    RE::FormType::Armor
                });
                valuableCount++;
            }
        }

        // Phase 3: Index misc items (gems, artifacts, quest items, etc.)
        for (auto* misc : dh->GetFormArray<RE::TESObjectMISC>()) {
            if (!misc || misc->IsDeleted()) continue;
            auto name = misc->GetFullName();
            if (!name || name[0] == '\0') continue;

            int goldValue = misc->GetGoldValue();
            if (IsGenericItem(name, goldValue)) continue;

            std::string lowerName = StringUtils::ToLowerStd(name);

            auto existing = m_itemsByName.find(lowerName);
            if (existing != m_itemsByName.end()) {
                auto* existingForm = RE::TESForm::LookupByID(existing->second);
                if (existingForm && existingForm->GetGoldValue() >= goldValue) {
                    continue;
                }
            }

            m_itemsByName[lowerName] = misc->GetFormID();
            totalIndexed++;

            if (goldValue >= VALUABLE_THRESHOLD) {
                m_valuableItems.push_back({
                    misc->GetFormID(),
                    std::string(name),
                    goldValue,
                    RE::FormType::Misc
                });
                valuableCount++;
            }
        }

        // Phase 4: Index spell tomes (TESObjectBOOK that teach spells)
        for (auto* book : dh->GetFormArray<RE::TESObjectBOOK>()) {
            if (!book || book->IsDeleted()) continue;
            if (!book->TeachesSpell()) continue;  // Only spell tomes, not regular books

            auto name = book->GetFullName();
            if (!name || name[0] == '\0') continue;

            int goldValue = book->GetGoldValue();
            if (IsGenericItem(name, goldValue)) continue;

            std::string lowerName = StringUtils::ToLowerStd(name);

            auto existing = m_itemsByName.find(lowerName);
            if (existing != m_itemsByName.end()) {
                auto* existingForm = RE::TESForm::LookupByID(existing->second);
                if (existingForm && existingForm->GetGoldValue() >= goldValue) {
                    continue;
                }
            }

            m_itemsByName[lowerName] = book->GetFormID();
            totalIndexed++;

            if (goldValue >= VALUABLE_THRESHOLD) {
                m_valuableItems.push_back({
                    book->GetFormID(),
                    std::string(name),
                    goldValue,
                    RE::FormType::Book
                });
                valuableCount++;
            }
        }

        // Build name list for fuzzy search
        m_allNames.reserve(m_itemsByName.size());
        for (auto& [name, _] : m_itemsByName) {
            m_allNames.push_back(name);
        }

        m_indexBuilt = true;
        logger::info("[ItemIndex] Built: {} items indexed, {} valuable (>={} gold)",
                    totalIndexed, valuableCount, VALUABLE_THRESHOLD);
    }

    RE::TESBoundObject* ItemIndex::FindByName(const std::string& searchTerm) {
        if (searchTerm.empty()) return nullptr;

        std::shared_lock lock(m_mutex);

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);

        // 1. Exact match
        auto it = m_itemsByName.find(lowerSearch);
        if (it != m_itemsByName.end()) {
            auto* form = RE::TESForm::LookupByID(it->second);
            if (form) {
                return form->As<RE::TESBoundObject>();
            }
        }

        // 2. Fuzzy match (Levenshtein distance)
        auto fuzzyResult = StringUtils::FuzzyFind(lowerSearch, m_allNames, 3);
        if (fuzzyResult) {
            auto fit = m_itemsByName.find(fuzzyResult.match);
            if (fit != m_itemsByName.end()) {
                auto* form = RE::TESForm::LookupByID(fit->second);
                if (form) {
                    logger::info("[ItemIndex] Fuzzy match: '{}' -> '{}' (dist={})",
                                searchTerm, fuzzyResult.match, fuzzyResult.distance);
                    return form->As<RE::TESBoundObject>();
                }
            }
        }

        // 3. Substring match
        for (auto& name : m_allNames) {
            if (name.find(lowerSearch) != std::string::npos ||
                lowerSearch.find(name) != std::string::npos) {
                auto sit = m_itemsByName.find(name);
                if (sit != m_itemsByName.end()) {
                    auto* form = RE::TESForm::LookupByID(sit->second);
                    if (form) {
                        logger::info("[ItemIndex] Substring match: '{}' -> '{}'",
                                    searchTerm, name);
                        return form->As<RE::TESBoundObject>();
                    }
                }
            }
        }

        logger::warn("[ItemIndex] FindByName: '{}' not found", searchTerm);
        return nullptr;
    }

    bool ItemIndex::ValidateName(const std::string& name) {
        if (name.empty()) return false;

        std::shared_lock lock(m_mutex);

        std::string lowerSearch = StringUtils::ToLowerStd(name);

        // Exact match check
        if (m_itemsByName.count(lowerSearch) > 0) return true;

        // Fuzzy match check (allow distance <= 2 for validation)
        auto fuzzyResult = StringUtils::FuzzyFind(lowerSearch, m_allNames, 2);
        return static_cast<bool>(fuzzyResult);
    }

    std::string ItemIndex::GetRandomValuableName(int minGoldValue) {
        std::shared_lock lock(m_mutex);

        // Filter to items above the minimum value
        std::vector<const ItemEntry*> candidates;
        for (auto& entry : m_valuableItems) {
            if (entry.goldValue >= minGoldValue) {
                candidates.push_back(&entry);
            }
        }

        if (candidates.empty()) {
            // Fall back to any valuable item if min is too high
            for (auto& entry : m_valuableItems) {
                candidates.push_back(&entry);
            }
        }

        if (candidates.empty()) {
            logger::warn("[ItemIndex] GetRandomValuableName: no valuable items in index");
            return "";
        }

        // Pick randomly
        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        return candidates[dist(rng)]->name;
    }

    std::string ItemIndex::GetRandomValuableName(int minGoldValue,
            const std::unordered_set<std::string>& excludeNames) {
        std::shared_lock lock(m_mutex);

        // Build candidate list excluding recent items
        std::vector<const ItemEntry*> candidates;
        for (auto& entry : m_valuableItems) {
            if (entry.goldValue >= minGoldValue) {
                std::string lowerName = StringUtils::ToLowerStd(entry.name);
                if (excludeNames.find(lowerName) == excludeNames.end()) {
                    candidates.push_back(&entry);
                }
            }
        }

        // Fall back: try any valuable item (still excluding recents)
        if (candidates.empty()) {
            for (auto& entry : m_valuableItems) {
                std::string lowerName = StringUtils::ToLowerStd(entry.name);
                if (excludeNames.find(lowerName) == excludeNames.end()) {
                    candidates.push_back(&entry);
                }
            }
        }

        // Final fallback: ignore exclusions entirely (better than returning nothing)
        if (candidates.empty()) {
            for (auto& entry : m_valuableItems) {
                candidates.push_back(&entry);
            }
        }

        if (candidates.empty()) {
            logger::warn("[ItemIndex] GetRandomValuableName: no valuable items in index");
            return "";
        }

        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        return candidates[dist(rng)]->name;
    }

    size_t ItemIndex::GetItemCount() const {
        std::shared_lock lock(m_mutex);
        return m_allNames.size();
    }

}  // namespace IntelEngine
