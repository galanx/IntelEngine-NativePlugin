/**
 * NPC Index Implementation
 *
 * Builds and maintains an indexed database of all game NPCs.
 */

#include "NPCIndex.h"
#include "StringUtils.h"
#include "ProcessUtils.h"
#include "Settings.h"

namespace IntelEngine {

    void NPCIndex::BuildIndex() {
        std::unique_lock lock(m_mutex);

        logger::info("Building NPC index (worldwide)...");

        m_npcIndex.clear();
        m_allNames.clear();
        m_npcFormIds.clear();
        m_npcCurrentLocations.clear();

        // PHASE 1: Index ALL NPC base forms from the entire game
        // This gives us FormIDs for every named NPC, even unloaded ones
        const auto& npcArray = RE::TESDataHandler::GetSingleton()->GetFormArray<RE::TESNPC>();

        for (auto* npcForm : npcArray) {
            if (!npcForm) continue;

            // Get the NPC's name
            auto name = npcForm->GetFullName();
            if (!name || strlen(name) == 0) continue;

            // Skip generic NPCs (no unique name)
            if (npcForm->IsUnique()) {
                std::string lowerName = StringUtils::ToLowerStd(name);

                // Store FormID (works even for unloaded NPCs)
                m_npcFormIds[lowerName] = npcForm->GetFormID();
                m_allNames.push_back(lowerName);

                // Note: npcForm IS the actor base (TESNPC) - no GetActorBase() needed
                // Home location can be determined from AI packages, but that's complex
                // For now we rely on the FormID index to find NPCs anywhere

                logger::debug("Indexed NPC (base form): {} -> {:X}", name, npcForm->GetFormID());
            }
        }

        logger::info("Phase 1 complete: {} unique NPCs indexed from base forms", m_allNames.size());

        // PHASE 2: Update index with loaded actor references (higher priority)
        // Search ALL four process list tiers to cover every actor the engine tracks
        ProcessUtils::ForEachLoadedActor([this](RE::Actor* actor) {
            IndexLoadedNPC(actor);
            return false;
        });

        m_indexBuilt = true;
        logger::info("NPC index complete: {} total NPCs, {} currently loaded",
                     m_allNames.size(), m_npcIndex.size());
    }

    void NPCIndex::IndexLoadedNPC(RE::Actor* actor) {
        if (!actor) return;

        auto name = actor->GetDisplayFullName();
        if (!name || strlen(name) == 0) return;

        std::string lowerName = StringUtils::ToLowerStd(name);

        // Update with actual Actor reference
        m_npcIndex[lowerName] = actor;

        // Update current location
        if (auto* cell = actor->GetParentCell()) {
            auto cellName = cell->GetName();
            if (cellName && strlen(cellName) > 0) {
                m_npcCurrentLocations[lowerName] = cellName;
            }
        }

        logger::debug("Indexed loaded NPC: {} (now accessible)", name);
    }

    void NPCIndex::RefreshIndex() {
        // Quick refresh - update loaded actors from ALL four process list tiers
        std::unique_lock lock(m_mutex);

        ProcessUtils::ForEachLoadedActor([this](RE::Actor* actor) {
            IndexLoadedNPC(actor);
            return false;
        });

        logger::info("NPC index refreshed: {} NPCs", m_npcIndex.size());
    }

    void NPCIndex::RebuildIndex() {
        BuildIndex();
    }

    RE::Actor* NPCIndex::FindByName(const std::string& searchTerm) {
        std::shared_lock lock(m_mutex);

        if (searchTerm.empty()) return nullptr;

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);
        std::string matchedName;

        // 1. Exact match in loaded NPCs
        auto exactIt = m_npcIndex.find(lowerSearch);
        if (exactIt != m_npcIndex.end() && exactIt->second) {
            logger::debug("FindNPCByName('{}') -> Exact match (loaded)", searchTerm);
            return exactIt->second;
        }

        // 2. Fuzzy match with Levenshtein distance
        int maxDist = Settings::GetSingleton()->fuzzyMatchThreshold;
        auto fuzzy = StringUtils::FuzzyFind(lowerSearch, m_allNames, maxDist);

        if (fuzzy) {
            matchedName = fuzzy.match;
            // First try loaded actor
            auto loadedIt = m_npcIndex.find(matchedName);
            if (loadedIt != m_npcIndex.end() && loadedIt->second) {
                logger::debug("FindNPCByName('{}') -> Fuzzy match '{}' (loaded, distance={})",
                             searchTerm, matchedName, fuzzy.distance);
                return loadedIt->second;
            }

            // Not loaded - try to get from FormID
            auto formIdIt = m_npcFormIds.find(matchedName);
            if (formIdIt != m_npcFormIds.end()) {
                auto* actor = GetActorFromFormId(formIdIt->second);
                if (actor) {
                    logger::debug("FindNPCByName('{}') -> Fuzzy match '{}' (resolved from FormID)",
                                 searchTerm, matchedName);
                    return actor;
                }
            }
        }

        // 4. Partial substring match
        for (const auto& name : m_allNames) {
            if (name.find(lowerSearch) != std::string::npos ||
                lowerSearch.find(name) != std::string::npos) {

                auto loadedIt = m_npcIndex.find(name);
                if (loadedIt != m_npcIndex.end() && loadedIt->second) {
                    logger::debug("FindNPCByName('{}') -> Partial match '{}' (loaded)", searchTerm, name);
                    return loadedIt->second;
                }

                // Try FormID
                auto formIdIt = m_npcFormIds.find(name);
                if (formIdIt != m_npcFormIds.end()) {
                    auto* actor = GetActorFromFormId(formIdIt->second);
                    if (actor) {
                        logger::debug("FindNPCByName('{}') -> Partial match '{}' (resolved from FormID)",
                                     searchTerm, name);
                        return actor;
                    }
                }
            }
        }

        // 5. Live scan - search ALL currently loaded actors as last resort
        // This catches NPCs missed by the index (non-unique, newly spawned, etc.)
        RE::Actor* bestLiveMatch = nullptr;
        int bestLiveDist = maxDist + 1;

        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            auto displayName = actor->GetDisplayFullName();
            if (!displayName || strlen(displayName) == 0) return false;

            std::string lowerName = StringUtils::ToLowerStd(displayName);

            // Exact match — stop immediately
            if (lowerName == lowerSearch) {
                bestLiveMatch = actor;
                bestLiveDist = 0;
                return true;
            }

            // Fuzzy match
            int dist = StringUtils::LevenshteinDistance(lowerSearch, lowerName);
            if (dist < bestLiveDist) {
                bestLiveDist = dist;
                bestLiveMatch = actor;
            }

            // Substring match (treat as distance 1)
            if (lowerName.find(lowerSearch) != std::string::npos && 1 < bestLiveDist) {
                bestLiveDist = 1;
                bestLiveMatch = actor;
            }

            return false;
        });

        if (bestLiveMatch && bestLiveDist <= maxDist) {
            logger::info("FindNPCByName('{}') -> Live scan found '{}' (distance={})",
                        searchTerm, bestLiveMatch->GetDisplayFullName(), bestLiveDist);
            return bestLiveMatch;
        }

        logger::debug("FindNPCByName('{}') -> Not found (searched {} indexed + live scan)", searchTerm, m_allNames.size());
        return nullptr;
    }

    RE::FormID NPCIndex::FindFormIdByName(const std::string& searchTerm) {
        std::shared_lock lock(m_mutex);

        if (searchTerm.empty()) return 0;

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);

        // 1. Exact match
        auto exactIt = m_npcFormIds.find(lowerSearch);
        if (exactIt != m_npcFormIds.end()) {
            return exactIt->second;
        }

        // 2. Fuzzy match
        auto fuzzy = StringUtils::FuzzyFind(lowerSearch, m_allNames,
                                             Settings::GetSingleton()->fuzzyMatchThreshold);
        if (fuzzy) {
            auto it = m_npcFormIds.find(fuzzy.match);
            if (it != m_npcFormIds.end()) {
                return it->second;
            }
        }

        return 0;
    }

    RE::BSFixedString NPCIndex::GetNPCLocation(const std::string& searchTerm) {
        std::shared_lock lock(m_mutex);

        if (searchTerm.empty()) return "";

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);

        // Find the NPC name (with fuzzy matching)
        std::string matchedName;

        auto exactIt = m_npcCurrentLocations.find(lowerSearch);
        if (exactIt != m_npcCurrentLocations.end()) {
            return RE::BSFixedString(exactIt->second);
        }

        // Try fuzzy match
        int maxDist = Settings::GetSingleton()->fuzzyMatchThreshold;
        int bestDistance = maxDist + 1;

        for (const auto& [name, location] : m_npcCurrentLocations) {
            int dist = StringUtils::LevenshteinDistance(lowerSearch, name);
            if (dist < bestDistance) {
                bestDistance = dist;
                matchedName = name;
            }
        }

        if (!matchedName.empty() && bestDistance <= maxDist) {
            auto locIt = m_npcCurrentLocations.find(matchedName);
            if (locIt != m_npcCurrentLocations.end()) {
                return RE::BSFixedString(locIt->second);
            }
        }

        return "";
    }

    RE::Actor* NPCIndex::GetActorFromFormId(RE::FormID formId) {
        if (formId == 0) return nullptr;

        auto* form = RE::TESForm::LookupByID(formId);
        if (!form) return nullptr;

        // Try to get as Actor directly
        auto* actor = form->As<RE::Actor>();
        if (actor) return actor;

        // If it's an NPC base form, try to find a reference in ALL process list tiers
        auto* npc = form->As<RE::TESNPC>();
        if (npc) {
            RE::Actor* found = nullptr;
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* a) {
                auto* base = a->GetActorBase();
                if (base && base->GetFormID() == formId) {
                    found = a;
                    return true;
                }
                return false;
            });
            if (found) return found;
        }

        return nullptr;
    }

    bool NPCIndex::IsAccessible(RE::Actor* actor) {
        if (!actor) return false;

        // Check if disabled
        if (actor->IsDisabled()) return false;

        // Check if dead
        if (actor->IsDead()) return false;

        // Check if 3D loaded (in active cell)
        if (!actor->Is3DLoaded()) {
            // Not loaded, but might still be accessible
            // Check if in same worldspace as player
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                auto* actorCell = actor->GetParentCell();
                auto* playerCell = player->GetParentCell();
                if (actorCell && playerCell) {
                    // If in different interiors, might not be accessible
                    if (actorCell->IsInteriorCell() && playerCell->IsInteriorCell() &&
                        actorCell != playerCell) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    RE::BSFixedString NPCIndex::GetSuggestion(const std::string& searchTerm) {
        std::shared_lock lock(m_mutex);

        if (searchTerm.empty() || m_allNames.empty()) {
            return "";
        }

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);

        // Find closest match (distance <= 5 for suggestions)
        auto fuzzy = StringUtils::FuzzyFind(lowerSearch, m_allNames, 5);

        if (fuzzy) {
            // Return original case from the actor
            auto it = m_npcIndex.find(fuzzy.match);
            if (it != m_npcIndex.end() && it->second) {
                return it->second->GetDisplayFullName();
            }
        }

        return "";
    }

}  // namespace IntelEngine
