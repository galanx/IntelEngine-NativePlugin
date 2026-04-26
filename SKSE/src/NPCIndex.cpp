/**
 * NPC Index Implementation
 *
 * Builds and maintains an indexed database of all game NPCs.
 */

#include "NPCIndex.h"
#include <algorithm>
#include "CellAnalyzer.h"
#include "FactionPolitics.h"
#include "PoliticalDB.h"
#include "LocationResolver.h"
#include "MemoryDB.h"
#include "SlotTracker.h"
#include "StringUtils.h"
#include "ProcessUtils.h"
#include "Settings.h"
#include "SkyrimNetAPI.h"

// SkyrimNetAPI.h pulls in Windows.h whose min/max macros conflict with std::min/std::max
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <random>
#include <cmath>
#include <chrono>
#include <mutex>
#include <sstream>

namespace IntelEngine {

    // ── Faction Blocklist (populated from plugin config) ──
    // Format: "FactionEditorID" blocks all ranks, "FactionEditorID:N" blocks rank <= N
    struct BlockedFaction {
        RE::TESFaction* faction = nullptr;
        int maxBlockedRank = -1;  // -1 = block all ranks
    };

    static std::mutex s_blocklistMutex;
    static std::vector<BlockedFaction> s_blockedFactions;
    static std::chrono::steady_clock::time_point s_lastBlocklistRefresh;

    static void RefreshFactionBlocklist() {
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(s_blocklistMutex);
            if (now - s_lastBlocklistRefresh < std::chrono::seconds(30)) return;
            s_lastBlocklistRefresh = now;
        }

        if (!SkyrimNetAPI::GetPluginConfigValue) return;
        std::string csv = SkyrimNetAPI::GetPluginConfigValue("IntelEngine", "story.faction_blocklist", "");

        // Build new list outside the lock, then swap in atomically
        std::vector<BlockedFaction> newList;
        if (!csv.empty()) {
            std::stringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
                size_t start = token.find_first_not_of(" \t");
                size_t end = token.find_last_not_of(" \t");
                if (start == std::string::npos) continue;
                std::string entry = token.substr(start, end - start + 1);
                if (entry.empty()) continue;

                // Parse optional rank: "FactionEditorID:maxRank"
                std::string factionId = entry;
                int maxRank = -1;  // default: block all ranks
                size_t colonPos = entry.find(':');
                if (colonPos != std::string::npos) {
                    factionId = entry.substr(0, colonPos);
                    try {
                        maxRank = std::stoi(entry.substr(colonPos + 1));
                    } catch (...) {
                        // Invalid rank number — fall back to blocking all ranks
                        logger::warn("Faction blocklist: invalid rank in '{}', blocking all ranks", entry);
                        maxRank = -1;
                    }
                }

                auto* form = RE::TESForm::LookupByEditorID(factionId);
                if (auto* faction = form ? form->As<RE::TESFaction>() : nullptr) {
                    newList.push_back({ faction, maxRank });
                } else {
                    logger::warn("Faction blocklist: '{}' not found as a faction", factionId);
                }
            }
        }

        std::lock_guard<std::mutex> lock(s_blocklistMutex);
        s_blockedFactions = std::move(newList);
        if (!s_blockedFactions.empty()) {
            logger::info("Faction blocklist refreshed: {} entries", s_blockedFactions.size());
        }
    }

    static bool IsInBlockedFaction(RE::Actor* actor) {
        std::lock_guard<std::mutex> lock(s_blocklistMutex);
        for (const auto& blocked : s_blockedFactions) {
            if (!actor->IsInFaction(blocked.faction)) continue;
            if (blocked.maxBlockedRank == -1) return true;  // block all ranks
            // Check rank from base NPC record (VisitFactions only sees runtime changes)
            auto* base = actor->GetActorBase();
            if (base) {
                for (const auto& fr : base->factions) {
                    if (fr.faction == blocked.faction && fr.rank <= blocked.maxBlockedRank) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // ── Danger Zone Policy ──
    void NPCIndex::SetDangerZonePolicy(int policy) {
        m_dangerZonePolicy.store(std::clamp(policy, 0, 3), std::memory_order_relaxed);
    }

    void NPCIndex::SetPlayerHomePolicy(int policy) {
        m_playerHomePolicy.store(std::clamp(policy, 0, 3), std::memory_order_relaxed);
    }

    void NPCIndex::SetHoldRestrictionPolicy(const std::string& storyType, int policy) {
        std::lock_guard<std::mutex> lock(m_holdRestrictionMutex);
        m_holdRestrictionPolicies[storyType] = std::clamp(policy, 0, 6);
        logger::info("Hold restriction policy: {} = {}", storyType, policy);
    }

    int NPCIndex::GetHoldRestrictionPolicy(const std::string& storyType) const {
        std::lock_guard<std::mutex> lock(m_holdRestrictionMutex);
        auto it = m_holdRestrictionPolicies.find(storyType);
        if (it != m_holdRestrictionPolicies.end()) return it->second;
        return 1;  // default: same hold for civilians
    }

    bool NPCIndex::IsPotentialFollower(RE::Actor* actor) {
        if (!actor) return false;
        static RE::TESFaction* s_potentialFollowerFaction = nullptr;
        if (!s_potentialFollowerFaction) {
            s_potentialFollowerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84D);
        }
        return s_potentialFollowerFaction && actor->IsInFaction(s_potentialFollowerFaction);
    }

    // ── Shared CSV Blocklist Refresh ──
    // Handles throttle (30s), config read, CSV parse, trim, lowercase, swap under lock.
    static void RefreshStringBlocklist(std::mutex& mtx, std::chrono::steady_clock::time_point& lastRefresh,
                                       const char* configKey, std::vector<std::string>& output, const char* label) {
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(mtx);
            if (now - lastRefresh < std::chrono::seconds(30)) return;
            lastRefresh = now;
        }
        if (!SkyrimNetAPI::GetPluginConfigValue) return;
        std::string csv = SkyrimNetAPI::GetPluginConfigValue("IntelEngine", configKey, "");

        // Strip surrounding quotes if YAML parser preserved them (e.g., '""' → '')
        if (csv.size() >= 2 && csv.front() == '"' && csv.back() == '"') {
            csv = csv.substr(1, csv.size() - 2);
        }

        std::vector<std::string> newList;
        if (!csv.empty()) {
            std::stringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
                auto start = token.find_first_not_of(" \t\"");
                auto end = token.find_last_not_of(" \t\"");
                if (start != std::string::npos && start <= end) {
                    std::string cleaned = StringUtils::ToLowerStd(token.substr(start, end - start + 1));
                    if (!cleaned.empty()) {
                        newList.push_back(cleaned);
                    }
                }
            }
        }
        std::lock_guard<std::mutex> lock(mtx);
        output = std::move(newList);
        logger::info("{} refreshed: {} entries (raw config: '{}')", label, output.size(), csv);
    }

    // ── Location Blocklist ──
    static std::vector<std::string> s_blockedLocations;
    static std::mutex s_locationMutex;
    static std::chrono::steady_clock::time_point s_locationBlocklistLastRefresh;

    static void RefreshLocationBlocklist() {
        RefreshStringBlocklist(s_locationMutex, s_locationBlocklistLastRefresh,
            "story.location_blocklist", s_blockedLocations, "Location blocklist");
    }

    bool NPCIndex::IsPlayerInBlockedLocation() {
        RefreshLocationBlocklist();
        std::lock_guard<std::mutex> lock(s_locationMutex);
        if (s_blockedLocations.empty()) return false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return false;
        RE::BSFixedString locName = LocationResolver::GetSingleton()->GetActorLocationName(player);
        std::string playerLoc = StringUtils::ToLowerStd(locName.c_str() ? locName.c_str() : "");
        for (const auto& blocked : s_blockedLocations) {
            if (playerLoc == blocked) return true;
        }
        return false;
    }

    // IsPlayerInWhitelistedLocation is defined after whitelist declarations below.

    // ── NPC Name Blocklist (populated from plugin config) ──
    static std::vector<std::string> s_blockedNPCNames;  // lowercase
    static std::mutex s_npcNameMutex;
    static std::chrono::steady_clock::time_point s_npcNameBlocklistLastRefresh;

    static void RefreshNPCNameBlocklist() {
        RefreshStringBlocklist(s_npcNameMutex, s_npcNameBlocklistLastRefresh,
            "story.npc_blocklist", s_blockedNPCNames, "NPC name blocklist");
    }

    static bool IsBlockedByName(RE::Actor* actor) {
        std::lock_guard<std::mutex> lock(s_npcNameMutex);
        if (s_blockedNPCNames.empty()) return false;
        auto displayName = actor->GetDisplayFullName();
        if (!displayName || !displayName[0]) return false;
        std::string nameLower = StringUtils::ToLowerStd(displayName);
        for (const auto& blocked : s_blockedNPCNames) {
            if (nameLower == blocked) return true;
        }
        return false;
    }

    // ── Whitelists (empty = disabled, non-empty = only listed entries pass) ──
    static std::vector<std::string> s_whitelistedNPCNames;  // lowercase
    static std::mutex s_npcWhitelistMutex;
    static std::chrono::steady_clock::time_point s_npcWhitelistLastRefresh;

    static void RefreshNPCWhitelist() {
        RefreshStringBlocklist(s_npcWhitelistMutex, s_npcWhitelistLastRefresh,
            "story.npc_whitelist", s_whitelistedNPCNames, "NPC whitelist");
    }

    static bool PassesNPCWhitelist(RE::Actor* actor) {
        RefreshNPCWhitelist();
        std::lock_guard<std::mutex> lock(s_npcWhitelistMutex);
        if (s_whitelistedNPCNames.empty()) return true;  // empty = everything allowed
        auto displayName = actor->GetDisplayFullName();
        if (!displayName || !displayName[0]) return false;
        std::string nameLower = StringUtils::ToLowerStd(displayName);
        for (const auto& allowed : s_whitelistedNPCNames) {
            if (nameLower == allowed) return true;
        }
        return false;
    }

    static std::vector<std::string> s_whitelistedLocations;  // lowercase
    static std::mutex s_locationWhitelistMutex;
    static std::chrono::steady_clock::time_point s_locationWhitelistLastRefresh;

    static void RefreshLocationWhitelist() {
        RefreshStringBlocklist(s_locationWhitelistMutex, s_locationWhitelistLastRefresh,
            "story.location_whitelist", s_whitelistedLocations, "Location whitelist");
    }

    bool NPCIndex::IsPlayerInWhitelistedLocation() {
        RefreshLocationWhitelist();
        std::lock_guard<std::mutex> lock(s_locationWhitelistMutex);
        if (s_whitelistedLocations.empty()) return true;  // empty = everything allowed

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return true;
        RE::BSFixedString locName = LocationResolver::GetSingleton()->GetActorLocationName(player);
        std::string playerLoc = StringUtils::ToLowerStd(locName.c_str() ? locName.c_str() : "");
        for (const auto& allowed : s_whitelistedLocations) {
            if (playerLoc == allowed) return true;
        }
        return false;
    }

    // Faction whitelist — uses EditorID lookup (same as blocklist but with whitelist logic)
    static std::vector<RE::TESFaction*> s_whitelistedFactions;
    static std::mutex s_factionWhitelistMutex;
    static std::chrono::steady_clock::time_point s_factionWhitelistLastRefresh;

    static void RefreshFactionWhitelist() {
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(s_factionWhitelistMutex);
            if (now - s_factionWhitelistLastRefresh < std::chrono::seconds(30)) return;
            s_factionWhitelistLastRefresh = now;
        }
        if (!SkyrimNetAPI::GetPluginConfigValue) return;
        std::string csv = SkyrimNetAPI::GetPluginConfigValue("IntelEngine", "story.faction_whitelist", "");

        std::vector<RE::TESFaction*> newList;
        if (!csv.empty()) {
            std::stringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start == std::string::npos) continue;
                std::string factionId = token.substr(start, end - start + 1);
                auto* form = RE::TESForm::LookupByEditorID(factionId);
                if (auto* faction = form ? form->As<RE::TESFaction>() : nullptr) {
                    newList.push_back(faction);
                }
            }
        }
        std::lock_guard<std::mutex> lock(s_factionWhitelistMutex);
        s_whitelistedFactions = std::move(newList);
        if (!s_whitelistedFactions.empty()) {
            logger::info("Faction whitelist refreshed: {} entries", s_whitelistedFactions.size());
        }
    }

    static bool PassesFactionWhitelist(RE::Actor* actor) {
        RefreshFactionWhitelist();
        std::lock_guard<std::mutex> lock(s_factionWhitelistMutex);
        if (s_whitelistedFactions.empty()) return true;  // empty = everything allowed
        for (auto* faction : s_whitelistedFactions) {
            if (actor->IsInFaction(faction)) return true;
        }
        return false;
    }

    // Shared time-of-day string from timescale-aware game time
    static const char* GetTimeOfDayString() {
        float gameTime = RE::Calendar::GetSingleton()->GetCurrentGameTime();
        float hour = (gameTime - std::floor(gameTime)) * 24.0f;
        if (hour < 5.0f)       return "late night";
        else if (hour < 8.0f)  return "early morning";
        else if (hour < 12.0f) return "morning";
        else if (hour < 14.0f) return "midday";
        else if (hour < 17.0f) return "afternoon";
        else if (hour < 20.0f) return "evening";
        else if (hour < 23.0f) return "night";
        else                    return "late night";
    }

    // Validate a cached Actor* from m_npcIndex is still alive.
    // Raw pointers in m_npcIndex become dangling when NPCs unload.
    // Cross-checks against the engine's form table to catch stale pointers.
    static RE::Actor* ValidateCachedActor(RE::Actor* cached) {
        if (!cached) return nullptr;
        auto formId = cached->GetFormID();
        if (formId == 0) return nullptr;
        auto* form = RE::TESForm::LookupByID(formId);
        if (form != cached || form->IsDeleted()) return nullptr;
        return cached;
    }

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

        // Shared resolver: upgrades base FormIDs to Actor reference FormIDs for unique NPCs
        auto resolveBaseFormIDs = [this](RE::TESObjectCELL* cell, int& count) {
            cell->ForEachReference([&](RE::TESObjectREFR& ref) {
                auto* actor = ref.As<RE::Actor>();
                if (!actor || actor->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;

                auto* base = actor->GetActorBase();
                if (!base || !base->IsUnique()) return RE::BSContainer::ForEachResult::kContinue;

                auto baseName = base->GetFullName();
                if (!baseName || strlen(baseName) == 0) return RE::BSContainer::ForEachResult::kContinue;

                std::string lowerName = StringUtils::ToLowerStd(baseName);
                auto it = m_npcFormIds.find(lowerName);
                if (it != m_npcFormIds.end()) {
                    if (it->second == base->GetFormID()) {
                        it->second = actor->GetFormID();
                        count++;
                    } else if (actor->GetFormID() > it->second) {
                        logger::warn("NPC '{}' has multiple persistent refs: 0x{:08X} supersedes 0x{:08X}",
                            baseName, actor->GetFormID(), it->second);
                        it->second = actor->GetFormID();
                    }
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
        };

        // PHASE 1.5: Resolve base FormIDs → Actor reference FormIDs via persistent cells
        // Unique NPCs have persistent references stored in worldspace persistent cells even when
        // unloaded. Scan ALL worldspaces (Tamriel, Solstheim, DLC, etc.) so NPCs in any worldspace
        // get resolved — not just NPCs in the player's current worldspace.
        int resolvedCount = 0;
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (dataHandler) {
            const auto& worldspaces = dataHandler->GetFormArray<RE::TESWorldSpace>();
            for (auto* worldspace : worldspaces) {
                if (!worldspace || !worldspace->persistentCell) continue;
                resolveBaseFormIDs(worldspace->persistentCell, resolvedCount);
            }
        }
        logger::info("Phase 1.5 complete: {} base FormIDs resolved to Actor reference FormIDs across all worldspaces", resolvedCount);

        // PHASE 1.6: Resolve remaining base FormIDs via interior cells
        // Some unique NPCs (especially mod-added) may have persistent references in standalone
        // interior cells that aren't part of any worldspace. Scan dataHandler->interiorCells
        // to catch these. Only resolves NPCs still at base FormID (not already resolved by 1.5).
        int interiorResolved = 0;
        if (dataHandler) {
            for (auto* cell : dataHandler->interiorCells) {
                if (!cell) continue;
                resolveBaseFormIDs(cell, interiorResolved);
            }
        }
        logger::info("Phase 1.6 complete: {} additional base FormIDs resolved via interior cells", interiorResolved);

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

        // Update FormID to Actor reference ID (base form ID won't cast to Actor in Papyrus)
        m_npcFormIds[lowerName] = actor->GetFormID();

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

        // Evict stale Actor* entries (NPCs that unloaded since last refresh)
        size_t evicted = 0;
        for (auto it = m_npcIndex.begin(); it != m_npcIndex.end(); ) {
            if (!ValidateCachedActor(it->second)) {
                it = m_npcIndex.erase(it);
                evicted++;
            } else {
                ++it;
            }
        }

        ProcessUtils::ForEachLoadedActor([this](RE::Actor* actor) {
            IndexLoadedNPC(actor);
            return false;
        });

        if (evicted > 0) {
            logger::info("NPC index refreshed: {} NPCs ({} stale evicted)", m_npcIndex.size(), evicted);
        } else {
            logger::info("NPC index refreshed: {} NPCs", m_npcIndex.size());
        }
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
        if (exactIt != m_npcIndex.end()) {
            if (auto* valid = ValidateCachedActor(exactIt->second)) {
                logger::debug("FindNPCByName('{}') -> Exact match (loaded)", searchTerm);
                return valid;
            }
            // Stale pointer — fall through to other strategies
        }

        // 2. Fuzzy match with Levenshtein distance
        int maxDist = Settings::GetSingleton()->fuzzyMatchThreshold;
        auto fuzzy = StringUtils::FuzzyFind(lowerSearch, m_allNames, maxDist);

        if (fuzzy) {
            matchedName = fuzzy.match;
            // First try loaded actor
            auto loadedIt = m_npcIndex.find(matchedName);
            if (loadedIt != m_npcIndex.end()) {
                if (auto* valid = ValidateCachedActor(loadedIt->second)) {
                    logger::debug("FindNPCByName('{}') -> Fuzzy match '{}' (loaded, distance={})",
                                 searchTerm, matchedName, fuzzy.distance);
                    return valid;
                }
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
                if (loadedIt != m_npcIndex.end()) {
                    if (auto* valid = ValidateCachedActor(loadedIt->second)) {
                        logger::debug("FindNPCByName('{}') -> Partial match '{}' (loaded)", searchTerm, name);
                        return valid;
                    }
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
            if (!actor || actor->IsDeleted()) return false;
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

    RE::Actor* NPCIndex::FindByNameNear(const std::string& searchTerm, RE::Actor* nearActor, bool allowSelf) {
        // If no actor context, fall back to original FindByName
        if (!nearActor) return FindByName(searchTerm);
        // Guard: actor may be unloaded (no cell = no valid position)
        if (!nearActor->GetParentCell()) return FindByName(searchTerm);

        std::shared_lock lock(m_mutex);
        if (searchTerm.empty()) return nullptr;

        std::string lowerSearch = StringUtils::ToLowerStd(searchTerm);
        int maxDist = Settings::GetSingleton()->fuzzyMatchThreshold;
        auto nearPos = nearActor->GetPosition();

        struct Candidate {
            RE::Actor* actor;
            int nameScore;   // 0=exact, 1=substring, 2+=fuzzy
            float distSq;    // 2D distance squared to nearActor
        };

        std::vector<Candidate> candidates;

        // Single pass through all loaded actors — collect all candidates
        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!actor || actor->IsDeleted()) return false;
            if (!allowSelf && actor == nearActor) return false;  // skip self unless allowed
            auto displayName = actor->GetDisplayFullName();
            if (!displayName || strlen(displayName) == 0) return false;

            std::string lowerName = StringUtils::ToLowerStd(displayName);
            int nameScore = -1;  // -1 = no match

            if (lowerName == lowerSearch) {
                nameScore = 0;  // exact
            } else if (lowerName.find(lowerSearch) != std::string::npos ||
                       lowerSearch.find(lowerName) != std::string::npos) {
                nameScore = 1;  // substring
            } else {
                int leven = StringUtils::LevenshteinDistance(lowerSearch, lowerName);
                if (leven <= maxDist) {
                    nameScore = 2 + leven;  // fuzzy
                }
            }

            if (nameScore >= 0) {
                auto pos = actor->GetPosition();
                float dx = pos.x - nearPos.x;
                float dy = pos.y - nearPos.y;
                candidates.push_back({actor, nameScore, dx * dx + dy * dy});
            }
            return false;
        });

        // Note: m_npcIndex is NOT used here — raw Actor* pointers can become dangling
        // when actors unload. ForEachLoadedActor above already covers all loaded actors safely.

        if (candidates.empty()) {
            logger::debug("FindByNameNear('{}') -> No candidates found", searchTerm);
            return nullptr;
        }

        // Pick best: lowest nameScore first, then lowest distSq as tiebreaker
        auto* best = &candidates[0];
        for (size_t i = 1; i < candidates.size(); ++i) {
            auto& c = candidates[i];
            if (c.nameScore < best->nameScore ||
                (c.nameScore == best->nameScore && c.distSq < best->distSq)) {
                best = &c;
            }
        }

        logger::debug("FindByNameNear('{}') -> '{}' (score={}, dist={:.0f}, {} candidates)",
                     searchTerm, best->actor->GetDisplayFullName(), best->nameScore,
                     std::sqrt(best->distSq), candidates.size());
        return best->actor;
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

        // Exact match
        auto exactIt = m_npcCurrentLocations.find(lowerSearch);
        if (exactIt != m_npcCurrentLocations.end()) {
            return RE::BSFixedString(exactIt->second);
        }

        // Fuzzy match — use FuzzyFind (consistent with rest of codebase)
        int maxDist = Settings::GetSingleton()->fuzzyMatchThreshold;
        auto fuzzy = StringUtils::FuzzyFind(lowerSearch, m_allNames, maxDist);
        if (fuzzy) {
            auto locIt = m_npcCurrentLocations.find(fuzzy.match);
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

        // Guard against stale pointers to freed/recycled memory.
        // GetFormID() and IsDeleted() are non-virtual member reads (offsets 0x14
        // and 0x10) — safe even on corrupted objects in committed heap memory.
        // If memory was zeroed, GetFormID() returns 0 != formId → early out.
        // If memory was recycled for a different form, FormID won't match.
        // Crash report: EXCEPTION_ACCESS_VIOLATION at 0x000000000000 when the
        // Papyrus VM called a virtual method on the stale Actor* we returned.
        if (form->GetFormID() != formId || form->IsDeleted()) {
            logger::warn("GetActorFromFormId(0x{:08X}): stale or deleted form, skipping", formId);
            return nullptr;
        }

        auto* actor = form->As<RE::Actor>();
        if (actor) return actor;

        // If it's an NPC base form, try to find a reference in ALL process list tiers
        auto* npc = form->As<RE::TESNPC>();
        if (npc) {
            RE::Actor* found = nullptr;
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* a) {
                if (!a || a->IsDeleted()) return false;
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

    RE::Actor* NPCIndex::ResolveStoryCandidate(const std::string& name) {
        std::shared_lock lock(m_mutex);
        std::string lowerName = StringUtils::ToLowerStd(name);

        // Check DM pool first (player-centric tick), then NPC pool (social tick)
        for (const auto& pool : {std::cref(m_dmCandidatePool), std::cref(m_npcCandidatePool)}) {
            auto it = pool.get().find(lowerName);
            if (it != pool.get().end() && it->second != 0) {
                auto* actor = GetActorFromFormId(it->second);
                if (actor) {
                    logger::debug("ResolveStoryCandidate('{}') -> exact pool match (0x{:08X})",
                        name, it->second);
                    return actor;
                }
            }
        }

        // Fallback to general name search if not in either pool (safety net)
        logger::debug("ResolveStoryCandidate('{}') -> not in pool, falling back to FindByName", name);
        lock.unlock();  // FindByName acquires its own lock
        return FindByName(name);
    }

    RE::Actor* NPCIndex::ResolveFromMemoryDB(RE::FormID formId, const std::string& name) {
        // Fast path: try the DB's FormID directly (works when FormID is still current)
        if (auto* actor = GetActorFromFormId(formId)) {
            return actor;
        }

        // Slow path: FormID is stale (mod update, base-form vs reference mismatch).
        // Fall back to fuzzy name search using the reliable display name from uuid_mappings.
        // See MemoryDB.cpp header comment for full bug documentation.
        if (!name.empty()) {
            auto* actor = FindByName(name);
            if (actor) {
                logger::debug("ResolveFromMemoryDB: stale FormID 0x{:08X} for '{}', "
                    "resolved via name to 0x{:08X}",
                    formId, name, actor->GetFormID());
            }
            return actor;
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
            if (it != m_npcIndex.end()) {
                if (auto* valid = ValidateCachedActor(it->second)) {
                    return valid->GetDisplayFullName();
                }
            }
        }

        return "";
    }

    // Generic creature/animal names that appear in SkyrimNet's MemoryDB because
    // events were recorded for them (combat, death, looting). These are not valid
    // story candidates — they lack ActorTypeNPC keyword in-game, but filtering
    // them here avoids wasting DB query slots and resolve+scoring cycles.
    static bool IsGenericCreatureName(const std::string& name) {
        static const std::unordered_set<std::string> kExcluded = {
            "Bear", "Bone Wolf", "Cave Bear", "Chicken", "Cow", "Dog",
            "Dragon", "Draugr", "Falmer", "Female Mammoth",
            "Frostbite Spider", "Giant", "Giant Youngling", "Goat",
            "Gypsy Horse", "Horse", "Ice Wolf", "Ice Wraith",
            "Mammoth", "Mammoth Calf", "Mudcrab", "Rabbit",
            "Sabre Cat", "Skeever", "Slaughterfish", "Snowy Sabre Cat",
            "Spriggan", "Thrall Wolf", "Troll", "Unknown", "Wisp",
            "Wolf", "Flame Atronach", "Frost Atronach", "Storm Atronach",
            "Hagraven", "Chaurus", "Horker", "Fox",
            "Dremora", "Dremora Lord", "Dremora Markynaz", "Dremora Caitiff",
            // Test cell actors — should never appear in story dispatch
            "TestTony", "Marker Storage Unit"
        };
        return kExcluded.count(name) > 0;
    }

    // Shared eligibility checks — used by both strict and relaxed candidate filtering.
    // Returns false (with optional debug log) if the actor fails any common check.
    static bool PassesCommonEligibility(RE::Actor* actor, RE::Actor* player, SlotTracker* tracker, bool verbose) {
        if (!actor || actor == player) return false;

        auto displayName = actor->GetDisplayFullName();
        if (!displayName || !displayName[0] || std::string_view(displayName) == "Unknown") {
            if (verbose) logger::debug("[StoryDM] Rejected 0x{:08X}: no display name", actor->GetFormID());
            return false;
        }
        if (actor->IsDead()) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': dead", displayName);
            return false;
        }
        if (actor->IsDisabled()) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': disabled", displayName);
            return false;
        }
        if (tracker && (tracker->HasActiveTask(actor) || tracker->IsOnCooldown(actor))) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': active task or cooldown", displayName);
            return false;
        }

        static auto* kwActorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);
        if (kwActorTypeNPC && !actor->HasKeyword(kwActorTypeNPC)) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': not ActorTypeNPC", displayName);
            return false;
        }
        if (auto* race = actor->GetRace()) {
            if (race->IsChildRace()) {
                if (verbose) logger::debug("[StoryDM] Rejected '{}': child race", displayName);
                return false;
            }
        }
        if (NPCIndex::GetSingleton()->IsOnStoryCooldown(actor->GetFormID(), NPCIndex::GetStoryCooldownHours())) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': story cooldown", displayName);
            return false;
        }

        RefreshFactionBlocklist();
        if (IsInBlockedFaction(actor)) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': blocked faction", displayName);
            return false;
        }
        RefreshNPCNameBlocklist();
        if (IsBlockedByName(actor)) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': blocked by name", displayName);
            return false;
        }

        // Whitelist checks (empty = disabled, non-empty = only listed entries pass)
        if (!PassesFactionWhitelist(actor)) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': not in faction whitelist", displayName);
            return false;
        }
        if (!PassesNPCWhitelist(actor)) {
            if (verbose) logger::debug("[StoryDM] Rejected '{}': not in NPC whitelist", displayName);
            return false;
        }

        // Policy-based filtering (danger zone + player home)
        auto checkPolicy = [&](int policy, bool condition, const char* label) -> bool {
            if (policy > 0 && condition) {
                if (policy == 3) {
                    if (verbose) logger::debug("[StoryDM] Rejected '{}': {} (block all)", displayName, label);
                    return false;
                }
                if (policy == 2 && !NPCIndex::IsPotentialFollower(actor)) {
                    if (verbose) logger::debug("[StoryDM] Rejected '{}': {} (followers only)", displayName, label);
                    return false;
                }
                if (policy == 1 && NPCIndex::ClassifyNPCArchetype(actor) == "CIVILIAN") {
                    if (verbose) logger::debug("[StoryDM] Rejected '{}': {} (civilian)", displayName, label);
                    return false;
                }
            }
            return true;
        };

        auto* cellAnalyzer = CellAnalyzer::GetSingleton();
        auto* npcIdx = NPCIndex::GetSingleton();
        if (!checkPolicy(npcIdx->GetDangerZonePolicy(), cellAnalyzer->IsPlayerInDangerousLocation(), "danger zone")) return false;
        if (!checkPolicy(npcIdx->GetPlayerHomePolicy(), cellAnalyzer->IsPlayerInOwnHome(), "player home")) return false;

        return true;
    }

    bool NPCIndex::IsEligibleStoryCandidate(RE::Actor* actor, RE::Actor* player,
        RE::TESObjectCELL* playerCell, SlotTracker* tracker) {
        if (!PassesCommonEligibility(actor, player, tracker, false)) return false;

        // Strict-only checks: deleted, no parent cell, in combat, teammate, hostile, same cell
        if (actor->IsDeleted()) return false;
        if (!actor->GetParentCell()) return false;
        if (actor->IsInCombat()) return false;
        if (actor->IsPlayerTeammate()) return false;
        if (actor->IsHostileToActor(player)) return false;
        if (actor->GetParentCell() == playerCell) return false;

        return true;
    }

    bool NPCIndex::IsEligibleStoryCandidateRelaxed(RE::Actor* actor, RE::Actor* player,
        SlotTracker* tracker) {
        if (!PassesCommonEligibility(actor, player, tracker, true)) return false;
        // Followers should never be story candidates — they're already with the player
        if (actor->IsPlayerTeammate()) return false;
        return true;
    }

    std::string NPCIndex::GetNPCLocationName(RE::Actor* actor) {
        if (!actor) return "";

        // Loaded: use current cell/location
        if (auto* cell = actor->GetParentCell()) {
            auto cellName = cell->GetName();
            if (cellName && cellName[0]) return cellName;

            if (auto* loc = actor->GetCurrentLocation()) {
                auto locName = loc->GetFullName();
                if (locName && locName[0]) return locName;
            }
        }

        // Unloaded: fall back to editor location
        if (auto* editorLoc = actor->GetEditorLocation()) {
            auto locName = editorLoc->GetFullName();
            if (locName && locName[0]) return locName;
        }

        return "";
    }

    // Shared 3-step location resolution: parentCell → saveParentCell → editorLocation
    static RE::BGSLocation* ResolveActorLocation(RE::Actor* actor) {
        if (!actor) return nullptr;
        RE::BGSLocation* loc = nullptr;
        if (auto* cell = actor->GetParentCell()) {
            loc = cell->GetLocation();
        }
        if (!loc) {
            if (auto* saveCell = actor->GetSaveParentCell()) {
                loc = saveCell->GetLocation();
            }
        }
        if (!loc) {
            loc = actor->GetEditorLocation();
        }
        return loc;
    }

    std::string NPCIndex::GetNPCHoldName(RE::Actor* actor) {
        auto* loc = ResolveActorLocation(actor);
        if (!loc) return "";

        // Walk up but stop at the hold level — the topmost is "Tamriel" (worldspace), one too far.
        // Stop when the parent's parent is null (parent = worldspace = top).
        while (loc->parentLoc && loc->parentLoc->parentLoc) {
            loc = loc->parentLoc;
        }
        auto name = loc->GetFullName();
        return (name && name[0]) ? name : "";
    }

    std::string NPCIndex::GetActorSettlementName(RE::Actor* actor) {
        auto* loc = ResolveActorLocation(actor);
        if (!loc) return "";

        // Walk UP the hierarchy looking for LocTypeCity or LocTypeTown keyword.
        // C++11 guarantees thread-safe initialization of function-local statics (magic statics).
        static auto* s_cityKW = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeCity");
        static auto* s_townKW = RE::TESForm::LookupByEditorID<RE::BGSKeyword>("LocTypeTown");

        RE::BGSLocation* current = loc;
        while (current) {
            if ((s_cityKW && current->HasKeyword(s_cityKW)) ||
                (s_townKW && current->HasKeyword(s_townKW))) {
                auto name = current->GetFullName();
                return (name && name[0]) ? name : "";
            }
            current = current->parentLoc;
        }

        // No city/town keyword found — NPC is in wilderness/unnamed area.
        // Return the immediate location name as fallback.
        auto name = loc->GetFullName();
        return (name && name[0]) ? name : "";
    }

    RE::Actor* NPCIndex::GetRandomStoryCandidate() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return nullptr;
        auto* tracker = SlotTracker::GetSingleton();

        std::vector<RE::Actor*> candidates;
        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (IsEligibleStoryCandidate(actor, player, playerCell, tracker)) {
                candidates.push_back(actor);
            }
            return false;
        });

        if (candidates.empty()) return nullptr;

        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
        return candidates[dist(rng)];
    }

    RE::Actor* NPCIndex::GetMemoryDrivenCandidate() {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return nullptr;
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return nullptr;
        auto* tracker = SlotTracker::GetSingleton();
        auto* memDB = MemoryDB::GetSingleton();

        float absenceHours = Settings::GetSingleton()->storyMinAbsenceDays * 24.0f;
        // Name-based exclusion set (lowercase) — handles dual UUID + stale FormID bugs
        auto recentPlayerNPCs = memDB->GetRecentPlayerInteractionNames(absenceHours);

        auto ranked = memDB->GetRankedCandidateFormIDs(30);
        for (const auto& [formId, name, score] : ranked) {
            // Exclusion check by lowercase name (matches the LOWER() output from SQL)
            if (recentPlayerNPCs.count(StringUtils::ToLowerStd(name))) continue;

            // Resolve via FormID first, fall back to name if FormID is stale
            auto* actor = ResolveFromMemoryDB(formId, name);
            if (!IsEligibleStoryCandidate(actor, player, playerCell, tracker)) continue;

            logger::info("GetMemoryDrivenCandidate: '{}' (score: {:.1f})",
                        actor->GetDisplayFullName(), score);
            return actor;
        }

        logger::debug("GetMemoryDrivenCandidate: falling back to random");
        return GetRandomStoryCandidate();
    }

    RE::Actor* NPCIndex::GetRelatedCandidate(RE::Actor* relatedTo) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !relatedTo) return nullptr;
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return nullptr;
        auto* tracker = SlotTracker::GetSingleton();
        auto* memDB = MemoryDB::GetSingleton();

        float absenceHours = Settings::GetSingleton()->storyMinAbsenceDays * 24.0f;
        // Name-based exclusion set (lowercase) — handles dual UUID + stale FormID bugs
        auto recentPlayerNPCs = memDB->GetRecentPlayerInteractionNames(absenceHours);

        // Phase 1: NPCs sharing event history with relatedTo
        auto ranked = memDB->GetRelatedCandidateFormIDs(relatedTo->GetFormID(), 15);
        for (const auto& [formId, name, score] : ranked) {
            if (recentPlayerNPCs.count(StringUtils::ToLowerStd(name))) continue;

            auto* actor = ResolveFromMemoryDB(formId, name);
            if (!actor || actor == relatedTo) continue;
            if (!IsEligibleStoryCandidate(actor, player, playerCell, tracker)) continue;

            logger::info("GetRelatedCandidate: '{}' related to '{}' (score: {:.1f})",
                        actor->GetDisplayFullName(), relatedTo->GetDisplayFullName(), score);
            return actor;
        }

        // Phase 2: any ranked NPC excluding relatedTo
        auto fallback = memDB->GetRankedCandidateFormIDs(30);
        for (const auto& [formId, name, score] : fallback) {
            if (recentPlayerNPCs.count(StringUtils::ToLowerStd(name))) continue;

            auto* actor = ResolveFromMemoryDB(formId, name);
            if (!actor || actor == relatedTo) continue;
            if (!IsEligibleStoryCandidate(actor, player, playerCell, tracker)) continue;

            logger::info("GetRelatedCandidate: '{}' (fallback, score: {:.1f})",
                        actor->GetDisplayFullName(), score);
            return actor;
        }

        logger::debug("GetRelatedCandidate: no related candidates found");
        return nullptr;
    }

    RE::Actor* NPCIndex::FindMessengerForSender(RE::Actor* sender) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !sender) return nullptr;
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return nullptr;
        auto* tracker = SlotTracker::GetSingleton();

        auto isValidMessenger = [&](RE::Actor* actor) -> bool {
            // Messengers must be common NPCs. Jarls, court wizards, stewards, and
            // housecarls outrank any sender and wouldn't physically run errands —
            // excluding them here prevents absurd substitutions like "Jarl Balgruuf
            // delivers a note on behalf of Farengar".
            return actor && actor != sender && actor != player &&
                   !IsHighStatus(actor) &&
                   IsEligibleStoryCandidate(actor, player, playerCell, tracker);
        };

        // Phase 1: Household members (same home cell via bed ownership index)
        auto* locResolver = LocationResolver::GetSingleton();
        if (locResolver) {
            auto household = locResolver->GetHouseholdMembers(sender);
            if (!household.empty()) {
                // Resolve base FormIDs to TESNPC pointers for fast comparison
                std::vector<RE::TESNPC*> householdBases;
                householdBases.reserve(household.size());
                for (RE::FormID baseFormId : household) {
                    auto* npcBase = RE::TESForm::LookupByID<RE::TESNPC>(baseFormId);
                    if (npcBase) householdBases.push_back(npcBase);
                }

                // Single scan: find any loaded actor matching a household base
                std::vector<RE::Actor*> householdActors;
                ProcessUtils::ForEachLoadedActor([&](RE::Actor* a) {
                    auto* base = a->GetActorBase();
                    for (auto* hBase : householdBases) {
                        if (base == hBase) { householdActors.push_back(a); break; }
                    }
                    return false;
                });

                for (auto* actor : householdActors) {
                    if (isValidMessenger(actor)) {
                        logger::info("FindMessengerForSender: household '{}' for '{}'",
                            actor->GetDisplayFullName(), sender->GetDisplayFullName());
                        return actor;
                    }
                }
            }
        }

        // Phase 2: Social associates (MemoryDB shared event history — same hold only)
        std::string senderHold = GetNPCHoldName(sender);
        auto* memDB = MemoryDB::GetSingleton();
        if (memDB) {
            auto ranked = memDB->GetRelatedCandidateFormIDs(sender->GetFormID(), 10);
            for (const auto& [formId, name, score] : ranked) {
                auto* actor = ResolveFromMemoryDB(formId, name);
                if (isValidMessenger(actor) && !senderHold.empty() && GetNPCHoldName(actor) == senderHold) {
                    logger::info("FindMessengerForSender: associate '{}' for '{}' (score: {:.1f})",
                        actor->GetDisplayFullName(), sender->GetDisplayFullName(), score);
                    return actor;
                }
            }
        }

        // Phases 3-5: Single loaded-actor scan for guards AND civilians.
        // Phase 3 = same-hold guard, Phase 4 = same-hold civilian, Phase 5 =
        // any-hold civilian/guard (last-resort when the sender's hold has no
        // loaded eligible messengers — e.g. player is in Whiterun interior
        // with Farengar; all Whiterun guards share the cell and get rejected).
        std::vector<RE::Actor*> guards;
        std::vector<RE::Actor*> sameHoldCivilians;
        std::vector<RE::Actor*> anyHoldMessengers;
        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!isValidMessenger(actor)) return false;
            std::string archetype = ClassifyNPCArchetype(actor);
            bool sameHold = !senderHold.empty() && GetNPCHoldName(actor) == senderHold;
            if (archetype == "GUARD" && sameHold) {
                guards.push_back(actor);
            } else if (archetype == "CIVILIAN" && sameHold) {
                sameHoldCivilians.push_back(actor);
            } else if (archetype == "CIVILIAN" || archetype == "GUARD") {
                anyHoldMessengers.push_back(actor);
            }
            return false;
        });

        // Phase 3: Prefer same-hold guard
        if (!guards.empty()) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, guards.size() - 1);
            auto* guard = guards[dist(rng)];
            logger::info("FindMessengerForSender: guard '{}' from {} for '{}'",
                guard->GetDisplayFullName(), senderHold, sender->GetDisplayFullName());
            return guard;
        }

        // Phase 4: Same-hold civilian
        if (!sameHoldCivilians.empty()) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, sameHoldCivilians.size() - 1);
            auto* civ = sameHoldCivilians[dist(rng)];
            logger::info("FindMessengerForSender: civilian '{}' for '{}'",
                civ->GetDisplayFullName(), sender->GetDisplayFullName());
            return civ;
        }

        // Phase 5: Last resort — any loaded civilian or guard, regardless of hold.
        // Breaks perfect hold fidelity but keeps dispatch from stalling on
        // "Courier" when the sender's hold has nobody else loaded and eligible.
        if (!anyHoldMessengers.empty()) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<size_t> dist(0, anyHoldMessengers.size() - 1);
            auto* msg = anyHoldMessengers[dist(rng)];
            logger::info("FindMessengerForSender: any-hold fallback '{}' ({}) for '{}'",
                msg->GetDisplayFullName(), GetNPCHoldName(msg), sender->GetDisplayFullName());
            return msg;
        }

        logger::info("FindMessengerForSender: no messenger found for '{}'",
            sender->GetDisplayFullName());
        return nullptr;
    }

    std::string NPCIndex::ClassifyNPCArchetype(RE::Actor* actor) {
        if (!actor) return "CIVILIAN";

        auto* base = actor->GetActorBase();
        if (!base) return "CIVILIAN";

        // Primary: pass through the CK class name directly to the LLM.
        // Only known civilian classes get mapped to CIVILIAN — everything else
        // is returned uppercased so the DM LLM can interpret it contextually.
        if (auto* cls = base->npcClass) {
            auto raw = cls->GetFullName();
            if (raw && raw[0]) {
                std::string classLower = StringUtils::ToLowerStd(raw);

                // Known civilian classes → CIVILIAN
                if (classLower == "citizen" || classLower == "farmer" ||
                    classLower == "beggar" || classLower == "child" ||
                    classLower == "bard's college" || classLower == "food vendor" ||
                    classLower == "peddler" || classLower == "apothecary" ||
                    classLower == "blacksmith" || classLower == "fence" ||
                    classLower == "innkeeper" || classLower == "lumberjack" ||
                    classLower == "miner" || classLower == "vendor")
                    return "CIVILIAN";

                // Everything else: uppercase the raw class name
                std::string result(raw);
                for (auto& c : result) {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                return result;
            }
        }

        // No class name — fall back to skill-based classification
        auto* avo = actor->AsActorValueOwner();
        if (!avo) return "CIVILIAN";
        float combat = avo->GetActorValue(RE::ActorValue::kOneHanded) +
                       avo->GetActorValue(RE::ActorValue::kTwoHanded) +
                       avo->GetActorValue(RE::ActorValue::kBlock);
        float magic  = avo->GetActorValue(RE::ActorValue::kDestruction) +
                       avo->GetActorValue(RE::ActorValue::kConjuration) +
                       avo->GetActorValue(RE::ActorValue::kRestoration);
        float stealth = avo->GetActorValue(RE::ActorValue::kSneak) +
                        avo->GetActorValue(RE::ActorValue::kPickpocket) +
                        avo->GetActorValue(RE::ActorValue::kLockpicking);

        float best = std::max({combat, magic, stealth});
        if (best >= 75.0f) {
            if (combat == best) return "WARRIOR";
            if (magic == best) return "MAGE";
            return "ROGUE";
        }

        return "CIVILIAN";
    }

    bool NPCIndex::IsJarl(RE::Actor* actor) {
        if (!actor) return false;

        // Cache the faction pointer
        static RE::TESFaction* s_jarlFaction = nullptr;
        static bool s_looked = false;
        if (!s_looked) {
            s_jarlFaction = RE::TESForm::LookupByEditorID<RE::TESFaction>("JobJarlFaction");
            s_looked = true;
            if (!s_jarlFaction)
                logger::warn("IsJarl: JobJarlFaction not found by editor ID");
        }

        // IsInFaction checks both base NPC record and runtime faction changes
        // (VisitFactions only visits runtime ExtraFactionChanges, missing base factions)
        if (s_jarlFaction && actor->IsInFaction(s_jarlFaction))
            return true;

        // Name-based fallback for modded games that strip vanilla factions
        auto name = actor->GetDisplayFullName();
        if (name && std::string_view(name).substr(0, 5) == "Jarl ") {
            logger::debug("IsJarl: '{}' matched by name prefix (faction check failed)", name);
            return true;
        }

        return false;
    }

    bool NPCIndex::IsHighStatus(RE::Actor* actor) {
        if (!actor) return false;

        // Check Jarl first (already cached)
        if (IsJarl(actor)) return true;

        // Cache high-status faction pointers (lazy init, game data doesn't change)
        static std::vector<RE::TESFaction*> s_highStatusFactions;
        static bool s_initialized = false;
        if (!s_initialized) {
            s_initialized = true;
            const char* factionEditorIDs[] = {
                "JobCourtWizardFaction",
                "JobStewardFaction",
                "JobHousecarlFaction",
            };
            for (auto editorID : factionEditorIDs) {
                auto* faction = RE::TESForm::LookupByEditorID<RE::TESFaction>(editorID);
                if (faction) {
                    s_highStatusFactions.push_back(faction);
                    logger::debug("IsHighStatus: cached faction '{}'", editorID);
                } else {
                    logger::warn("IsHighStatus: faction '{}' not found", editorID);
                }
            }
        }

        if (s_highStatusFactions.empty()) return false;

        // IsInFaction checks both base NPC record and runtime faction changes
        for (auto* hsFaction : s_highStatusFactions) {
            if (actor->IsInFaction(hsFaction)) return true;
        }
        return false;
    }

    // Check if an NPC passes the hold restriction policy for a given story type.
    // Returns true if the NPC is allowed to be dispatched for this type.
    bool NPCIndex::PassesHoldRestriction(RE::Actor* actor, const std::string& playerHold, int policy) {
        if (policy == 0) return true;  // no restriction
        if (playerHold.empty()) return true;  // can't determine player hold, allow

        // Policies 4-5: settlement-level restriction (city/town granularity)
        if (policy >= 4) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            std::string playerSettlement = player ? GetActorSettlementName(player) : "";
            std::string npcSettlement = GetActorSettlementName(actor);

            // Can't determine either → allow (graceful degradation)
            if (playerSettlement.empty() || npcSettlement.empty()) return true;
            if (playerSettlement == npcSettlement) return true;  // same settlement → always passes

            // Different settlement
            if (policy == 6) return false;  // nobody crosses settlements
            if (policy == 5) return NPCIndex::IsPotentialFollower(actor);  // only followers cross settlements
            // policy == 4: civilians blocked, warriors can cross within same hold
            if (NPCIndex::ClassifyNPCArchetype(actor) == "CIVILIAN") return false;
            // Warriors: allow if same hold, block if different hold
            std::string npcHold = GetNPCHoldName(actor);
            if (npcHold.empty()) return true;
            return npcHold == playerHold;
        }

        std::string npcHold = NPCIndex::GetNPCHoldName(actor);
        if (npcHold.empty()) return true;  // can't determine NPC hold, allow

        // Same hold — always passes (policies 1-3)
        if (npcHold == playerHold) return true;

        // Different hold — check policy
        // 1 = block civilians only, 2 = block everyone except followers, 3 = block everyone
        if (policy == 3) return false;
        if (policy == 2) return NPCIndex::IsPotentialFollower(actor);
        if (policy == 1) return NPCIndex::ClassifyNPCArchetype(actor) != "CIVILIAN";
        return true;
    }

    std::string NPCIndex::GetEligibleStoryTypes(RE::Actor* actor,
        const std::string& archetype, bool dangerous, bool interior) {

        // show_ flags from the Papyrus exclude list are handled separately in the prompt.
        // This function produces per-candidate constraints based on WHO the NPC is and
        // WHERE the player is. The DM must pick a type that appears in BOTH the global
        // show_ list AND this candidate's eligible list.

        bool isCivilian = (archetype == "CIVILIAN");

        // High-status NPCs (Jarls, stewards, court wizards, housecarls):
        // message + quest only — they NEVER travel personally.
        // For quest, the DM prompt enforces courier mode (NPC as sender, not courier).
        if (IsHighStatus(actor)) {
            auto name = actor->GetDisplayFullName();
            logger::debug("GetEligibleStoryTypes: '{}' is high-status -> message, quest only", name ? name : "?");
            return "message, quest";
        }

        // Get player hold for hold restriction checks
        std::string playerHold;
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) playerHold = GetNPCHoldName(player);

        auto* npcIndex = GetSingleton();

        std::vector<const char*> types;

        // seek_player: civilians can't enter danger zones + hold restriction
        if (!(isCivilian && dangerous) &&
            PassesHoldRestriction(actor, playerHold, npcIndex->GetHoldRestrictionPolicy("seek_player")))
            types.push_back("seek_player");

        // informant: never in danger zones + hold restriction
        if (!dangerous &&
            PassesHoldRestriction(actor, playerHold, npcIndex->GetHoldRestrictionPolicy("informant")))
            types.push_back("informant");

        // road_encounter: exterior only + hold restriction
        if (!interior &&
            PassesHoldRestriction(actor, playerHold, npcIndex->GetHoldRestrictionPolicy("road_encounter")))
            types.push_back("road_encounter");

        // ambush: combat-capable only + hold restriction
        if (!isCivilian &&
            PassesHoldRestriction(actor, playerHold, npcIndex->GetHoldRestrictionPolicy("ambush")))
            types.push_back("ambush");

        // stalker: exterior only, not civilian + hold restriction
        if (!interior && !isCivilian &&
            PassesHoldRestriction(actor, playerHold, npcIndex->GetHoldRestrictionPolicy("stalker")))
            types.push_back("stalker");

        // message: anyone can send a message + hold restriction
        if (PassesHoldRestriction(actor, playerHold, npcIndex->GetHoldRestrictionPolicy("message")))
            types.push_back("message");

        // quest: anyone can give a quest + hold restriction
        if (PassesHoldRestriction(actor, playerHold, npcIndex->GetHoldRestrictionPolicy("quest")))
            types.push_back("quest");

        std::string result;
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) result += ", ";
            result += types[i];
        }
        return result;
    }

    // Engine-touch fallback only — race + vanilla factions from game data.
    // Used by Phase A (main thread) so the snapshot captures a fast value without
    // hitting the bio-file disk I/O. Phase B then prefers the file-based bio if
    // GetNPCBioSummary returns non-empty for this NPC.
    std::string NPCIndex::GetNPCBioFallbackLine(RE::Actor* actor) {
        if (!actor) return "";
        auto* base = actor->GetActorBase();
        if (!base) return "";

        std::string result;

        if (auto* race = base->GetRace()) {
            auto raceName = race->GetFullName();
            if (raceName && raceName[0]) {
                result = raceName;
            }
        }

        std::vector<std::string> factionNames;
        for (const auto& fr : base->factions) {
            if (!fr.faction) continue;

            // Only include vanilla factions (Skyrim.esm = mod index 0)
            // Filters out modded factions (VectorPlexus, Schlongified, etc.)
            uint8_t modIdx = static_cast<uint8_t>(fr.faction->GetFormID() >> 24);
            if (modIdx != 0) continue;

            auto editorId = fr.faction->GetFormEditorID();
            if (!editorId || !editorId[0]) continue;
            std::string eid(editorId);

            if (eid.find("CrimeFaction") != std::string::npos) continue;
            if (eid.find("CurrentFollower") != std::string::npos) continue;
            if (eid.find("PotentialFollower") != std::string::npos) continue;
            if (eid.find("PotentialMarriage") != std::string::npos) continue;
            if (eid.find("WIPlayer") != std::string::npos) continue;
            if (eid.find("WINever") != std::string::npos) continue;
            if (eid.find("Job") == 0) continue;
            if (eid.find("TownFarm") != std::string::npos) continue;
            if (eid.find("ServicesRent") != std::string::npos) continue;
            if (eid.find("Favor") != std::string::npos) continue;
            if (eid.find("defaultDisallow") != std::string::npos) continue;

            auto dispName = fr.faction->GetFullName();
            if (dispName && dispName[0]) {
                factionNames.push_back(dispName);
            } else {
                if (eid.size() > 7 && eid.substr(eid.size() - 7) == "Faction") {
                    eid = eid.substr(0, eid.size() - 7);
                }
                factionNames.push_back(eid);
            }

            if (factionNames.size() >= 4) break;
        }

        if (!factionNames.empty()) {
            if (!result.empty()) result += " | ";
            for (size_t i = 0; i < factionNames.size(); ++i) {
                if (i > 0) result += ", ";
                result += factionNames[i];
            }
        }

        return result;
    }

    // Sync wrapper preserving the original main-thread semantics: prefer the rich
    // SkyrimNet bio summary (file I/O), fall back to engine race+factions.
    // Async paths use GetNPCBioFallbackLine in Phase A and GetNPCBioSummary in Phase B
    // to keep the file I/O off the main thread.
    std::string NPCIndex::GetNPCBioLine(RE::Actor* actor) {
        if (!actor) return "";
        auto* memDB = MemoryDB::GetSingleton();
        if (memDB) {
            std::string bio = memDB->GetNPCBioSummary(actor->GetFormID());
            if (!bio.empty()) return bio;
        }
        return GetNPCBioFallbackLine(actor);
    }

    // Helper: convert elapsed days to human-readable time string
    static std::string FormatElapsedDays(float daysSince) {
        if (daysSince < 0.042f) return "just now";
        if (daysSince < 0.5f) return "earlier today";
        if (daysSince < 1.f) return "yesterday";
        if (daysSince < 3.f) return std::to_string(static_cast<int>(daysSince)) + " days ago";
        if (daysSince < 7.f) return "several days ago";
        if (daysSince < 30.f) return "weeks ago";
        return "a long time ago";
    }

    std::string NPCIndex::BuildDungeonMasterContext(int maxCandidates, float absenceDays) {
        // Sync wrapper: runs Phase 0 (SQL prefetch), Phase A (snapshot), and Phase B
        // (build) inline on the calling thread. Preserves stale-bytecode behavior;
        // single source of truth shared with the async path.
        auto prefetch = FetchStoryDMPhaseAPrefetch(maxCandidates, absenceDays);
        return BuildDungeonMasterContextFromSnapshot(
            BuildStoryDMTickSnapshot(maxCandidates, absenceDays, prefetch));
    }

    std::string NPCIndex::BuildNPCInteractionContext(int maxPairs) {
        // Sync wrapper — delegates to async-path snapshot+phase-B for single source of truth.
        return BuildNPCInteractionContextFromSnapshot(BuildNPCTickSnapshot(maxPairs));
    }

    // =========================================================================
    // Async NPC interaction tick — Phase A (snapshot, main thread)
    // =========================================================================

    NPCIndex::NPCTickSnapshot NPCIndex::BuildNPCTickSnapshot(int maxPairs) {
        NPCTickSnapshot snap;
        snap.maxPairs = maxPairs;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return snap;
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return snap;

        snap.playerCellFormId = playerCell->GetFormID();
        if (auto* nm = player->GetDisplayFullName()) snap.playerName = nm;

        auto* locResolver = LocationResolver::GetSingleton();
        snap.playerLocation = locResolver->GetActorLocationName(player).c_str();
        if (snap.playerLocation.empty()) snap.playerLocation = "Unknown";

        snap.timeOfDayString = GetTimeOfDayString();

        auto* cal = RE::Calendar::GetSingleton();
        snap.currentGameTime = cal ? cal->GetCurrentGameTime() : 0.f;

        auto* tracker = SlotTracker::GetSingleton();
        static auto* kwActorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);
        const float storyCooldownHours = GetStoryCooldownHours();

        snap.candidates.reserve(64);

        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!actor || actor == player) return false;
            auto* cell = actor->GetParentCell();
            if (!cell) return false;
            if (actor->IsDead() || actor->IsDisabled()) return false;
            if (actor->IsInCombat()) return false;
            if (actor->IsHostileToActor(player)) return false;
            if (tracker && (tracker->HasActiveTask(actor) || tracker->IsOnCooldown(actor))) return false;
            if (kwActorTypeNPC && !actor->HasKeyword(kwActorTypeNPC)) return false;
            // Exclude active followers — they're with the player, not independently socializing.
            if (actor->IsPlayerTeammate()) return false;

            auto displayName = actor->GetDisplayFullName();
            if (!displayName || !displayName[0]) return false;
            std::string nameStr(displayName);
            if (IsGenericCreatureName(nameStr)) return false;

            if (auto* race = actor->GetRace()) {
                if (race->IsChildRace()) return false;
            }

            // Cooldown reads use the in-memory cache + Calendar — fine on main thread.
            if (IsOnStoryCooldown(actor->GetFormID(), storyCooldownHours)) return false;
            if (IsOnSocialCooldown(actor->GetFormID())) return false;

            std::string loc = GetNPCLocationName(actor);
            if (loc.empty()) return false;
            if (loc == "Marker Storage Unit" || loc == "TestTony") return false;

            NPCTickActorSnapshot a;
            a.formId       = actor->GetFormID();
            a.cellFormId   = cell->GetFormID();
            a.nameDisplay  = std::move(nameStr);
            a.nameLower    = StringUtils::ToLowerStd(a.nameDisplay);
            a.location     = std::move(loc);
            a.archetype    = ClassifyNPCArchetype(actor);
            // Capture only the engine-touch fallback here (cheap, no disk I/O).
            // Phase B prefers the file-based bio summary when available — moves the
            // ~10-15ms-per-file reads off the main thread on first encounter.
            a.bio          = GetNPCBioFallbackLine(actor);
            a.isFollower   = false;  // already filtered followers above
            a.isFemale     = false;
            if (auto* base = actor->GetActorBase()) {
                if (base->GetSex() == RE::SEX::kFemale) a.isFemale = true;
            }
            snap.candidates.push_back(std::move(a));
            return false;
        });

        return snap;
    }

    // =========================================================================
    // Async NPC interaction tick — Phase B (markdown, worker thread)
    // =========================================================================

    std::string NPCIndex::BuildNPCInteractionContextFromSnapshot(const NPCTickSnapshot& snap) {
        if (snap.candidates.empty()) return "";

        auto* memDB = MemoryDB::GetSingleton();

        // SQL: social score lookup (worker-thread safe).
        std::unordered_map<std::string, float> socialScoreLookup;
        auto socialNPCs = memDB->GetSociallyActiveFormIDs(20);
        for (const auto& [formId, name, score] : socialNPCs) {
            socialScoreLookup[StringUtils::ToLowerStd(name)] = score;
        }

        // Group by location.
        struct GroupedActor {
            const NPCTickActorSnapshot* actor;
            float socialScore;
        };
        std::unordered_map<std::string, std::vector<GroupedActor>> locationGroups;
        for (const auto& a : snap.candidates) {
            float socialScore = 0.f;
            auto it = socialScoreLookup.find(a.nameLower);
            if (it != socialScoreLookup.end()) socialScore = it->second;
            std::string locKey = StringUtils::ToLowerStd(a.location);
            locationGroups[locKey].push_back({&a, socialScore});
        }

        // Score and filter: only locations with 2+ NPCs form candidate groups.
        struct ScoredGroup {
            std::string locationDisplay;
            std::vector<GroupedActor> npcs;
            float groupScore;
            bool playerNearby;
        };
        std::vector<ScoredGroup> groups;

        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> noiseDist(0.0f, 1.0f);

        for (auto& [locKey, npcs] : locationGroups) {
            if (npcs.size() < 2) continue;

            float groupScore = static_cast<float>(npcs.size());
            for (const auto& n : npcs) groupScore += n.socialScore * 0.3f;
            groupScore += noiseDist(rng);

            bool nearPlayer = false;
            for (const auto& n : npcs) {
                if (n.actor->cellFormId == snap.playerCellFormId) {
                    nearPlayer = true;
                    break;
                }
            }
            if (nearPlayer) groupScore += 2.0f;

            groups.push_back({npcs[0].actor->location, std::move(npcs), groupScore, nearPlayer});
        }

        if (groups.empty()) {
            logger::debug("[NPCTick] No location groups with 2+ NPCs");
            return "";
        }

        std::sort(groups.begin(), groups.end(),
                  [](const ScoredGroup& a, const ScoredGroup& b) { return a.groupScore > b.groupScore; });
        if (static_cast<int>(groups.size()) > snap.maxPairs) {
            groups.resize(snap.maxPairs);
        }

        // Update NPC candidate pool for name->FormID resolution from response.
        {
            std::unique_lock poolLock(m_mutex);
            m_npcCandidatePool.clear();
            for (const auto& group : groups) {
                for (const auto& n : group.npcs) {
                    m_npcCandidatePool[n.actor->nameLower] = n.actor->formId;
                }
            }
        }

        // --- Build markdown ---
        std::string md;
        md.reserve(1024);

        md += "## World State\n";
        md += "- Player: ";  md += snap.playerName;
        md += " at ";        md += snap.playerLocation;  md += "\n";
        md += "- Time: ";    md += snap.timeOfDayString; md += "\n\n";

        // Political climate. Pass snap.currentGameTime so BuildPoliticalSummary doesn't
        // touch RE::Calendar from this worker thread.
        auto politicalSummary = FactionPolitics::GetSingleton()->BuildPoliticalSummary(snap.currentGameTime);
        if (!politicalSummary.empty()) {
            md += politicalSummary;
            md += "\n";
        }

        md += "## NPC Groups by Location\n";

        for (const auto& group : groups) {
            md += "### ";  md += group.locationDisplay;
            md += " (player nearby: ";  md += group.playerNearby ? "yes" : "no";  md += ")\n";

            int npcCount = std::min(static_cast<int>(group.npcs.size()), 3);
            for (int i = 0; i < npcCount; ++i) {
                const auto* a = group.npcs[i].actor;
                const char* gender = a->isFemale ? "Female" : "Male";

                md += "- ";   md += a->nameDisplay;
                md += " [";   md += a->archetype;  md += ", ";  md += gender;  md += "]";
                if (a->isFollower) md += " (follower)";
                // Prefer file-based bio summary (loaded on this worker thread, no main-thread
                // stutter on first encounter); fall back to engine-touch line snapshotted in Phase A.
                std::string bioForMd = memDB->GetNPCBioSummary(a->formId);
                if (bioForMd.empty()) bioForMd = a->bio;
                if (!bioForMd.empty()) {
                    md += " {";  md += bioForMd;  md += "}";
                }

                auto memories = memDB->GetFormattedMemories(a->formId, 2);
                if (!memories.empty()) {
                    md += ": ";  md += memories;
                }
                md += "\n";

                // World knowledge applicable to this NPC — keeps NPC-to-NPC
                // gossip and interactions current with major world facts
                // (faction leader deaths, dragon attacks, etc.). 2 entries per
                // NPC × ≤3 NPCs × ≤4 groups = 24 cheap-path calls max.
                auto knowledge = memDB->GetWorldKnowledgeForActor(a->formId, 2);
                if (!knowledge.empty()) {
                    md += "  Knows:";
                    for (const auto& k : knowledge) {
                        md += " ";  md += k;
                        if (!k.empty() && k.back() != '.') md += ".";
                    }
                    md += "\n";
                }
            }
            md += "\n";
        }

        static const std::unordered_set<std::string> npcTypes = {
            "npc_interaction", "npc_gossip"
        };
        auto typeCounts = GetStoryTypeCountsMarkdown(npcTypes);
        if (!typeCounts.empty()) {
            md += "## Story Type Picks This Session\n";
            md += typeCounts;
            md += "\n\n";
        }

        logger::info("[NPCTick] async context: {} groups, {} chars", groups.size(), md.size());
        return MemoryDB::EscapeJsonString(md);
    }

    // =========================================================================
    // Async Story DM tick — Phase A (snapshot, main thread)
    // =========================================================================

    static void FillStoryDMActorSnapshot(NPCIndex::StoryDMActorSnapshot& a, RE::Actor* actor,
                                          RE::FormID dbFormId, float dbScore, bool fromMemoryDB) {
        a.formId       = actor->GetFormID();
        a.dbFormId     = dbFormId;
        a.dbScore      = dbScore;
        a.fromMemoryDB = fromMemoryDB;

        if (auto* nm = actor->GetDisplayFullName()) a.nameDisplay = nm;
        a.nameLower    = StringUtils::ToLowerStd(a.nameDisplay);

        a.location     = NPCIndex::GetNPCLocationName(actor);
        if (a.location.empty()) a.location = "Unknown";

        a.archetype    = NPCIndex::ClassifyNPCArchetype(actor);
        // Engine-touch fallback only — Phase B prefers MemoryDB::GetNPCBioSummary
        // (file I/O moved off main thread to eliminate first-encounter stutter).
        a.bio          = NPCIndex::GetNPCBioFallbackLine(actor);

        a.npcHold      = NPCIndex::GetActorSettlementName(actor);
        if (a.npcHold.empty()) a.npcHold = NPCIndex::GetNPCHoldName(actor);
        if (a.npcHold.empty()) a.npcHold = "Unknown";

        if (auto* base = actor->GetActorBase()) {
            if (base->GetSex() == RE::SEX::kFemale) a.isFemale = true;
        }

        a.is3DLoaded   = actor->Is3DLoaded();
        if (a.is3DLoaded) {
            a.posX = actor->GetPositionX();
            a.posY = actor->GetPositionY();
        }

        // Pre-resolve political faction (engine-touch — must be on main thread)
        auto npcFaction = FactionPolitics::GetSingleton()->GetNPCFaction(actor);
        if (npcFaction) a.factionName = npcFaction->name;
    }

    // Phase 0 — worker thread. Pre-fetch the SQL inputs Phase A needs.
    // Both calls go through SkyrimNet's PublicAPI (thread-safe SQLite).
    NPCIndex::StoryDMPhaseAPrefetch NPCIndex::FetchStoryDMPhaseAPrefetch(int maxCandidates, float absenceDays) {
        StoryDMPhaseAPrefetch p;
        auto* memDB = MemoryDB::GetSingleton();
        if (!memDB) return p;
        const float absenceHours = absenceDays * 24.f;
        p.recentPlayerNPCs = memDB->GetRecentPlayerInteractionNames(absenceHours);
        p.ranked = memDB->GetRankedCandidateFormIDs(maxCandidates * 4);
        return p;
    }

    NPCIndex::StoryDMTickSnapshot NPCIndex::BuildStoryDMTickSnapshot(int maxCandidates, float absenceDays,
                                                                     const StoryDMPhaseAPrefetch& prefetch) {
        StoryDMTickSnapshot snap;
        snap.maxCandidates = maxCandidates;
        snap.absenceDays   = absenceDays;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return snap;
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return snap;

        if (IsPlayerInBlockedLocation()) {
            logger::info("[StoryDM] Player at blocked location — skipping DM tick");
            return snap;
        }

        auto* tracker = SlotTracker::GetSingleton();
        auto* memDB = MemoryDB::GetSingleton();
        auto* locResolver = LocationResolver::GetSingleton();
        auto* cellAnalyzer = CellAnalyzer::GetSingleton();

        // Player snapshot
        if (auto* nm = player->GetDisplayFullName()) snap.playerName = nm;
        snap.playerLocation = locResolver->GetActorLocationName(player).c_str();
        if (snap.playerLocation.empty()) snap.playerLocation = "Unknown";
        snap.playerHold = GetNPCHoldName(player);
        if (snap.playerHold.empty()) snap.playerHold = "Unknown";
        snap.playerInteriorCell = playerCell->IsInteriorCell();
        snap.playerInDangerousLocation = cellAnalyzer->IsPlayerInDangerousLocation();
        snap.playerAtInn = FactionPolitics::IsPlayerAtInn();
        snap.playerX = player->GetPositionX();
        snap.playerY = player->GetPositionY();
        snap.timeOfDayString = GetTimeOfDayString();

        auto* calendar = RE::Calendar::GetSingleton();
        snap.currentGameTime = calendar ? calendar->GetCurrentGameTime() : 0.f;
        snap.currentDBHours  = memDB->GetCurrentDBHours();

        // Take shared_lock for the std::string copy — writers (NotifyStoryTypePicked)
        // hold unique_lock on m_mutex when mutating these fields.
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            snap.lastStoryDispatchGameTime = m_lastStoryDispatchGameTime;
            snap.lastStoryDispatchType     = m_lastStoryDispatchType;
            snap.recentDispatches.assign(m_recentDispatches.begin(), m_recentDispatches.end());
        }

        // Snapshot Settings on the main thread — Settings is non-atomic / unlocked
        // and may be rewritten from the main thread on config reload.
        snap.memoriesPerCandidate = std::min(Settings::GetSingleton()->maxMemoriesInContext, 2);

        // Pool of (actor, dbFormId, dbScore, fromMemoryDB) before snapshot conversion
        struct StagedCandidate {
            RE::Actor* actor;
            RE::FormID dbFormId;
            float      dbScore;
            bool       fromMemoryDB;
        };
        std::vector<StagedCandidate> staged;

        // SQL inputs come from Phase 0 (pre-fetched on worker thread).
        const auto& recentPlayerNPCs = prefetch.recentPlayerNPCs;
        const auto& ranked           = prefetch.ranked;

        // Pass 1: MemoryDB-ranked candidates (allows unloaded NPCs).
        int dbUnresolved = 0, dbIneligible = 0, dbPlayerCell = 0, dbAccepted = 0, dbCreature = 0;
        for (const auto& [formId, name, score] : ranked) {
            if (IsGenericCreatureName(name)) {
                dbCreature++;
                logger::info("[StoryDM Pass1] skipped-creature: '{}' (score={:.1f})", name, score);
                continue;
            }
            if (recentPlayerNPCs.count(StringUtils::ToLowerStd(name))) {
                logger::info("[StoryDM Pass1] skipped-recent: '{}' (score={:.1f})", name, score);
                continue;
            }
            auto* actor = ResolveFromMemoryDB(formId, name);
            if (!actor) {
                dbUnresolved++;
                logger::info("[StoryDM Pass1] skipped-unresolved: '{}' (formId=0x{:08X}, score={:.1f})", name, formId, score);
                continue;
            }
            if (!IsEligibleStoryCandidateRelaxed(actor, player, tracker)) {
                dbIneligible++;
                // IsEligibleStoryCandidateRelaxed logs rejection reason via PassesCommonEligibility(verbose=true),
                // but IsPlayerTeammate rejection is silent — log it here.
                if (actor->IsPlayerTeammate()) {
                    logger::info("[StoryDM Pass1] skipped-teammate: '{}' (score={:.1f})", name, score);
                } else {
                    logger::info("[StoryDM Pass1] skipped-ineligible: '{}' (score={:.1f}) — see Rejected log above", name, score);
                }
                continue;
            }
            if (actor->GetParentCell() && actor->GetParentCell() == playerCell) {
                dbPlayerCell++;
                logger::info("[StoryDM Pass1] skipped-playercell: '{}' (score={:.1f})", name, score);
                continue;
            }
            staged.push_back({actor, formId, score, true});
            dbAccepted++;
            logger::info("[StoryDM Pass1] ACCEPTED: '{}' (score={:.1f})", name, score);
        }
        logger::info("[StoryDM async] MemoryDB candidates: {} ranked, {} accepted, "
            "{} creature, {} unresolved, {} ineligible, {} player-cell",
            ranked.size(), dbAccepted, dbCreature, dbUnresolved, dbIneligible, dbPlayerCell);

        // Pass 2: random encounter slots (always 3, regardless of pool fullness).
        constexpr int RANDOM_ENCOUNTER_SLOTS = 3;
        {
            std::unordered_set<RE::FormID> stagedIds;
            for (auto& c : staged) stagedIds.insert(c.actor->GetFormID());

            int randomsAdded = 0;
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
                if (randomsAdded >= RANDOM_ENCOUNTER_SLOTS) return true;
                if (stagedIds.count(actor->GetFormID())) return false;
                if (recentPlayerNPCs.count(StringUtils::ToLowerStd(actor->GetDisplayFullName()))) return false;
                if (!IsEligibleStoryCandidate(actor, player, playerCell, tracker)) return false;
                staged.push_back({actor, actor->GetFormID(), 0.f, false});
                stagedIds.insert(actor->GetFormID());
                randomsAdded++;
                logger::info("[StoryDM Pass2] ACCEPTED-loaded: '{}'", actor->GetDisplayFullName());
                return false;
            });
            logger::info("[StoryDM Pass2] added {} random-encounter slots", randomsAdded);
        }

        // Pass 3: location-mate candidates (for npc_interaction/npc_gossip variety).
        {
            std::unordered_set<RE::FormID> stagedIds;
            for (auto& c : staged) stagedIds.insert(c.actor->GetFormID());

            std::unordered_set<std::string> poolLocations;
            for (auto& c : staged) {
                std::string loc = GetNPCLocationName(c.actor);
                if (!loc.empty() && loc != "Unknown") {
                    poolLocations.insert(StringUtils::ToLowerStd(loc));
                }
            }

            int locationMatesAdded = 0;
            constexpr int MAX_LOCATION_MATES = 4;
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
                if (locationMatesAdded >= MAX_LOCATION_MATES) return true;
                if (!actor || stagedIds.count(actor->GetFormID())) return false;
                std::string actorNameLower = StringUtils::ToLowerStd(actor->GetDisplayFullName());
                if (recentPlayerNPCs.count(actorNameLower)) return false;
                if (!IsEligibleStoryCandidate(actor, player, playerCell, tracker)) return false;
                std::string actorLoc = GetNPCLocationName(actor);
                if (actorLoc.empty()) return false;
                if (!poolLocations.count(StringUtils::ToLowerStd(actorLoc))) return false;
                staged.push_back({actor, actor->GetFormID(), 1.f, false});
                stagedIds.insert(actor->GetFormID());
                locationMatesAdded++;
                logger::info("[StoryDM Pass3] ACCEPTED-locmate: '{}' at '{}'", actor->GetDisplayFullName(), actorLoc);
                return false;
            });
            logger::info("[StoryDM Pass3] added {} location-mate slots (pool locations: {})",
                locationMatesAdded, poolLocations.size());
        }

        // Convert staged to value snapshots
        snap.candidates.reserve(staged.size());
        for (auto& s : staged) {
            StoryDMActorSnapshot a;
            FillStoryDMActorSnapshot(a, s.actor, s.dbFormId, s.dbScore, s.fromMemoryDB);
            snap.candidates.push_back(std::move(a));
        }
        return snap;
    }

    // =========================================================================
    // Async Story DM tick — Phase B (scoring + markdown, worker thread)
    // =========================================================================

    std::string NPCIndex::BuildDungeonMasterContextFromSnapshot(const StoryDMTickSnapshot& snap) {
        if (snap.candidates.empty()) return "";

        auto* memDB = MemoryDB::GetSingleton();

        // Player relationships (1 SQL)
        auto relationships = memDB->GetPlayerRelationshipData();
        std::unordered_map<std::string, PlayerRelationship> relMap;
        std::unordered_map<RE::FormID, PlayerRelationship> relMapById;
        for (const auto& rel : relationships) {
            relMap[StringUtils::ToLowerStd(rel.name)] = rel;
            if (rel.formId > 0) relMapById[rel.formId] = rel;
        }

        constexpr float ABSENCE_BONUS_WEIGHT   = 1.5f;
        constexpr float GEOGRAPHIC_BONUS       = 3.0f;
        constexpr float FRIEND_OF_FRIEND_BONUS = 2.0f;
        constexpr float NOVELTY_BONUS          = 4.0f;
        constexpr float RANDOM_NOISE_MAX       = 3.0f;
        constexpr int RANDOM_ENCOUNTER_SLOTS   = 3;

        // Friend-of-friend set (top 5 player friends -> their related NPCs)
        std::unordered_set<std::string> friendOfFriendNames;
        {
            auto relSorted = relationships;
            std::sort(relSorted.begin(), relSorted.end(),
                [](const PlayerRelationship& a, const PlayerRelationship& b) {
                    return a.interactionCount > b.interactionCount;
                });
            int friendsChecked = 0;
            for (const auto& rel : relSorted) {
                if (friendsChecked >= 5) break;
                auto related = memDB->GetRelatedCandidateFormIDs(rel.formId, 5);
                for (const auto& [fofId, fofName, fofScore] : related) {
                    if (fofId != 0x14 && !fofName.empty()) {
                        friendOfFriendNames.insert(StringUtils::ToLowerStd(fofName));
                    }
                }
                friendsChecked++;
            }
        }

        // Score
        struct ScoredCandidate {
            const StoryDMActorSnapshot* a;
            float score;
        };
        std::vector<ScoredCandidate> scored;
        scored.reserve(snap.candidates.size());

        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> noiseDist(0.0f, RANDOM_NOISE_MAX);

        for (const auto& a : snap.candidates) {
            float activity = std::log2(a.dbScore + 1.0f);
            float absenceBonus = 0.f, geoBonus = 0.f, fofBonus = 0.f, noveltyBonus = 0.f;

            auto relIt = relMap.find(a.nameLower);
            if (relIt != relMap.end() && relIt->second.interactionCount > 0) {
                float daysSinceLast = (snap.currentDBHours - relIt->second.lastInteractionHours) / 86400.0f;
                if (daysSinceLast < 0.f) daysSinceLast = 0.f;
                float absenceRatio = std::min(daysSinceLast / 30.f, 2.f);
                float depth = std::min(std::sqrt(static_cast<float>(relIt->second.interactionCount)), 15.f);
                absenceBonus = absenceRatio * depth * ABSENCE_BONUS_WEIGHT;
            }

            if (relIt == relMap.end()) {
                noveltyBonus = NOVELTY_BONUS;
            } else {
                noveltyBonus = NOVELTY_BONUS / (1.0f + static_cast<float>(relIt->second.interactionCount));
            }

            if (!snap.playerHold.empty() && !a.npcHold.empty() && a.npcHold == snap.playerHold) {
                geoBonus = GEOGRAPHIC_BONUS;
            }

            if (friendOfFriendNames.count(a.nameLower)) {
                fofBonus = FRIEND_OF_FRIEND_BONUS;
            }

            float noise = noiseDist(rng);
            float score = activity + absenceBonus + noveltyBonus + geoBonus + fofBonus + noise;
            scored.push_back({&a, score});
        }

        std::sort(scored.begin(), scored.end(),
            [](const ScoredCandidate& l, const ScoredCandidate& r) { return l.score > r.score; });

        // Split: top maxCandidates ranked + up to 3 random encounters
        std::vector<ScoredCandidate> rankedPool;
        std::vector<ScoredCandidate> randomPool;
        for (auto& s : scored) {
            if (s.a->fromMemoryDB) rankedPool.push_back(s);
            else                   randomPool.push_back(s);
        }
        if (static_cast<int>(rankedPool.size()) > snap.maxCandidates) rankedPool.resize(snap.maxCandidates);
        if (static_cast<int>(randomPool.size()) > RANDOM_ENCOUNTER_SLOTS) randomPool.resize(RANDOM_ENCOUNTER_SLOTS);

        std::vector<ScoredCandidate> finalPool = std::move(rankedPool);
        for (auto& s : randomPool) finalPool.push_back(s);

        if (finalPool.empty()) {
            logger::debug("[StoryDM async] No eligible candidates after scoring");
            return "";
        }

        // Update DM candidate pool for response resolution
        {
            std::unique_lock poolLock(m_mutex);
            m_dmCandidatePool.clear();
            for (const auto& s : finalPool) {
                m_dmCandidatePool[s.a->nameLower] = s.a->formId;
            }
        }

        // --- Build markdown ---
        std::string md;
        md.reserve(2048);

        md += "## World State\n";
        md += "- Player: ";  md += snap.playerName;
        md += " at ";        md += snap.playerLocation;  md += "\n";
        md += "- Danger: ";  md += snap.playerInDangerousLocation ? "DANGEROUS" : "SAFE";  md += "\n";
        md += "- Environment: ";  md += snap.playerInteriorCell ? "Interior" : "Exterior";  md += "\n";
        md += "- Hold: ";    md += snap.playerHold;  md += "\n";
        md += "- Time: ";    md += snap.timeOfDayString;  md += "\n";
        md += "- At Inn: ";  md += snap.playerAtInn ? "yes" : "no";  md += "\n";

        if (snap.lastStoryDispatchGameTime > 0.f) {
            float hoursSinceDispatch = (snap.currentGameTime - snap.lastStoryDispatchGameTime) * 24.f;
            std::string typeHint = snap.lastStoryDispatchType.empty()
                                       ? ""
                                       : " (type: " + snap.lastStoryDispatchType + ")";
            if (hoursSinceDispatch < 5.f) {
                md += "- Last story dispatched: ";
                md += std::to_string(static_cast<int>(hoursSinceDispatch));
                md += " game hours ago" + typeHint + " — recent, avoid dispatching the EXACT same type/subtype. A different subtype is fine; only consider types/subtypes listed under Available Story Types below.\n";
            } else if (hoursSinceDispatch < 24.f) {
                md += "- Last story dispatched: ";
                md += std::to_string(static_cast<int>(hoursSinceDispatch));
                md += " game hours ago" + typeHint + "\n";
            } else {
                float daysSince = hoursSinceDispatch / 24.f;
                md += "- Last story dispatched: ";
                md += std::to_string(static_cast<int>(daysSince));
                md += " game days ago — the world has been quiet, consider dispatching\n";
            }
        } else {
            md += "- Last story dispatched: never this session — the world is silent, consider dispatching\n";
        }
        md += "\n";

        // Recent Dispatch History — concrete record of the last few Story DM ticks
        // so the LLM can verify Rule 14 (vary dispatcher) and Rule 15 (don't strike
        // at the same beloved NPC twice in a row). Most recent first.
        // Format anchor for Rule 14: "<age> ago: <type>[/<subtype>] — <dispatcher> (<narration>) [STATUS]"
        if (!snap.recentDispatches.empty()) {
            md += "## Recent Dispatch History (most recent first — vary types/dispatchers)\n";
            md += "Format: `<age> ago: <type>[/<subtype>] — <dispatcher> (<narration>) [DISPATCHED|REJECTED — reason]`\n";
            for (const auto& d : snap.recentDispatches) {
                float hoursAgo = (snap.currentGameTime - d.gameTime) * 24.f;
                if (hoursAgo < 0.f) hoursAgo = 0.f;
                md += "- ";
                if (hoursAgo < 1.f) {
                    int mins = static_cast<int>(hoursAgo * 60.f);
                    if (mins < 1) mins = 1;
                    md += std::to_string(mins);
                    md += "m ago: ";
                } else if (hoursAgo < 24.f) {
                    md += std::to_string(static_cast<int>(hoursAgo));
                    md += "h ago: ";
                } else {
                    md += std::to_string(static_cast<int>(hoursAgo / 24.f));
                    md += "d ago: ";
                }
                md += d.type;
                if (!d.subType.empty()) { md += "/"; md += d.subType; }
                if (!d.npcName.empty()) { md += " — "; md += d.npcName; }
                if (!d.narration.empty()) {
                    // UTF-8-safe truncation: walk back from the byte cap to the
                    // previous lead byte so we don't split a multi-byte sequence.
                    md += " (";
                    if (d.narration.size() <= MAX_DISPATCH_NARRATION_CHARS) {
                        md += d.narration;
                    } else {
                        size_t cut = MAX_DISPATCH_NARRATION_CHARS;
                        while (cut > 0 && (static_cast<unsigned char>(d.narration[cut]) & 0xC0u) == 0x80u) {
                            --cut;
                        }
                        md += d.narration.substr(0, cut);
                        md += "...";
                    }
                    md += ")";
                }
                if (d.outcome == DispatchOutcome::Rejected) {
                    md += " [REJECTED";
                    if (!d.reason.empty()) { md += " — "; md += d.reason; }
                    md += "]";
                } else {
                    md += " [DISPATCHED]";
                }
                md += "\n";
            }
            md += "\n";
        }

        // Recent gossip — populated by Papyrus before each DM tick via SetRecentGossipContext
        {
            std::unique_lock lock(m_mutex);
            if (!m_recentGossipContext.empty()) {
                md += "## Recent Gossip\n";
                md += m_recentGossipContext;
                md += "\n\n";
            }
        }

        // Pass snap.currentGameTime so BuildPoliticalSummary doesn't touch RE::Calendar
        // from this worker thread.
        auto politicalSummary = FactionPolitics::GetSingleton()->BuildPoliticalSummary(snap.currentGameTime);
        if (!politicalSummary.empty()) {
            md += politicalSummary;
            md += "\n";
        }

        // Reverse so most-important candidates appear last (LLM attention bias)
        std::reverse(finalPool.begin(), finalPool.end());
        md += "## Candidate Pool\n\n";
        int memPerCandidate = snap.memoriesPerCandidate;

        for (int i = 0; i < static_cast<int>(finalPool.size()); ++i) {
            const auto* a = finalPool[i].a;
            const char* gender = a->isFemale ? "Female" : "Male";

            char uuid[16];
            snprintf(uuid, sizeof(uuid), "0x%08X", a->formId);

            // Distance string
            std::string distStr;
            if (a->is3DLoaded) {
                float dx = a->posX - snap.playerX;
                float dy = a->posY - snap.playerY;
                float dist = std::sqrt(dx * dx + dy * dy);
                if (dist < 500.f) distStr = "very close (same area)";
                else if (dist < 2000.f) distStr = "nearby";
                else if (dist < 5000.f) distStr = "moderate distance";
                else distStr = "far away";
            } else {
                distStr = (a->npcHold == snap.playerHold) ? "same hold (not loaded)" : "different hold (far)";
            }

            // Last interaction with player — try every data source until one works
            std::string lastInteractionStr = "never met";
            bool foundInteractionTime = false;
            for (int attempt = 0; attempt < 2 && !foundInteractionTime; attempt++) {
                const PlayerRelationship* relData = nullptr;
                if (attempt == 0) {
                    auto it = relMapById.find(a->dbFormId);
                    if (it != relMapById.end() && it->second.interactionCount > 0) relData = &it->second;
                } else {
                    auto it = relMap.find(a->nameLower);
                    if (it != relMap.end() && it->second.interactionCount > 0) relData = &it->second;
                }
                if (relData) {
                    float dbTimeSince = snap.currentDBHours - relData->lastInteractionHours;
                    float daysSince = dbTimeSince / 86400.0f;
                    lastInteractionStr = FormatElapsedDays(daysSince);
                    lastInteractionStr += " (" + std::to_string(relData->interactionCount) + " interactions)";
                    foundInteractionTime = true;
                }
            }
            if (!foundInteractionTime && SkyrimNetAPI::GetRecentEvents) {
                try {
                    auto evtJson = SkyrimNetAPI::GetRecentEvents(
                        a->dbFormId, 1, "direct_narration,custom_action,dialogue,dialogue_background,persistent_generic");
                    auto arr = nlohmann::json::parse(evtJson);
                    if (arr.is_array() && !arr.empty()) {
                        double evtTime = arr[0].value("gameTime", 0.0);
                        if (evtTime > 0.0) {
                            float dbTimeSince = snap.currentDBHours - static_cast<float>(evtTime);
                            float daysSince = dbTimeSince / 86400.0f;
                            lastInteractionStr = FormatElapsedDays(daysSince);
                            foundInteractionTime = true;
                        }
                    }
                } catch (...) {}
            }

            // Familiarity tier
            std::string familiarityStr = "stranger (never interacted)";
            {
                auto dialogue = memDB->GetRecentDialogueForActor(a->dbFormId, 1);
                if (!dialogue.empty()) {
                    familiarityStr = "acquainted (has spoken directly)";
                } else {
                    auto memories = memDB->GetFormattedMemories(a->dbFormId, 1);
                    if (!memories.empty()) {
                        familiarityStr = "acquainted (shared history)";
                    } else {
                        auto related = memDB->GetRelatedCandidateFormIDs(a->dbFormId, 15);
                        for (const auto& rel : related) {
                            if (rel.formId == 0x14) {
                                familiarityStr = "aware (seen nearby, never spoken)";
                                break;
                            }
                        }
                    }
                }
            }

            md += "### ";  md += std::to_string(i + 1);  md += ". ";  md += a->nameDisplay;
            md += " [";    md += a->archetype;  md += ", ";  md += gender;  md += "] - ";  md += a->location;
            md += " (";    md += uuid;          md += ")\n";
            md += "Hold: ";       md += a->npcHold;        md += "\n";
            md += "Distance: ";   md += distStr;           md += "\n";
            md += "Knows player: ";  md += familiarityStr; md += "\n";
            md += "Last met player: ";  md += lastInteractionStr;  md += "\n";
            // Prefer worker-thread bio file load over Phase A's engine fallback.
            std::string bioForMd = memDB->GetNPCBioSummary(a->formId);
            if (bioForMd.empty()) bioForMd = a->bio;
            if (!bioForMd.empty()) {
                md += "Bio: ";  md += bioForMd;  md += "\n";
            }

            if (!a->factionName.empty()) {
                md += "Faction: ";  md += a->factionName;  md += "\n";
            }

            auto bioRels = memDB->GetNPCBioRelationships(a->formId);
            if (!bioRels.empty()) {
                md += "Relationships:\n";  md += bioRels;  md += "\n";
            }

            auto memories = memDB->GetFormattedMemories(a->dbFormId, memPerCandidate);
            if (!memories.empty()) {
                md += "Memories:\n";  md += memories;  md += "\n";
            }

            auto dialogue = memDB->GetRecentDialogueForActor(a->dbFormId, 3);
            if (!dialogue.empty()) {
                md += "Last conversation:\n";  md += dialogue;  md += "\n";
            }

            auto recentEvents = memDB->GetRecentEventsForActor(a->dbFormId, 3);
            if (!recentEvents.empty()) {
                md += "Recent:\n";  md += recentEvents;  md += "\n";
            }

            // World knowledge — facts about this NPC's world that the author
            // flagged always_inject. Cheap path, ~5-8 candidates per build.
            auto knowledge = memDB->GetWorldKnowledgeForActor(a->dbFormId, 3);
            if (!knowledge.empty()) {
                md += "Knows:\n";
                for (const auto& k : knowledge) {
                    md += "- ";  md += k;  md += "\n";
                }
            }

            auto relatedNPCs = memDB->GetRelatedCandidateFormIDs(a->dbFormId, 5);
            if (!relatedNPCs.empty()) {
                md += "In-game connections: ";
                bool firstRel = true;
                for (const auto& rel : relatedNPCs) {
                    if (rel.formId == a->dbFormId) continue;
                    if (!firstRel) md += ", ";
                    md += rel.name;
                    firstRel = false;
                }
                md += "\n";
            }

            md += "\n";
        }

        static const std::unordered_set<std::string> dmTypes = {
            "seek_player", "informant", "road_encounter", "ambush", "stalker", "message", "quest"
        };
        auto typeCounts = GetStoryTypeCountsMarkdown(dmTypes);
        if (!typeCounts.empty()) {
            md += "## Story Type Picks This Session\n";
            md += typeCounts;
            md += "\n\n";
        }

        auto recentItems = GetRecentQuestItemsString();
        auto recentVictims = GetRecentRescueVictimsString();
        auto recentLocations = GetRecentQuestLocationsString();
        if (!recentItems.empty() || !recentVictims.empty() || !recentLocations.empty()) {
            md += "## Recent Quest History (avoid repeats)\n";
            if (!recentLocations.empty()) {
                md += "- Recent quest locations: ";  md += recentLocations;  md += "\n";
            }
            if (!recentItems.empty()) {
                md += "- Recent find_item targets: ";  md += recentItems;  md += "\n";
            }
            if (!recentVictims.empty()) {
                md += "- Recent rescue victims: ";  md += recentVictims;  md += "\n";
            }
            md += "\n";
        }

        logger::info("[StoryDM async] DM context: {} candidates, {} chars",
                     finalPool.size(), md.size());
        return MemoryDB::EscapeJsonString(md);
    }

    void NPCIndex::NotifyStoryTypePicked(const std::string& storyType) {
        std::unique_lock lock(m_mutex);
        m_storyTypeCounts[storyType]++;
        auto* cal = RE::Calendar::GetSingleton();
        float now = cal ? cal->GetCurrentGameTime() : 0.f;
        // NPC DM types tracked separately so Story DM doesn't see them as "last dispatch"
        if (storyType == "npc_interaction" || storyType == "npc_gossip") {
            m_lastNPCDispatchGameTime = now;
        } else {
            m_lastStoryDispatchType = storyType;
            m_lastStoryDispatchGameTime = now;
        }
    }

    namespace {
        // Strip markdown-structural / control characters from LLM-authored
        // strings (subType, npcName, narration, rejection reasons) so they
        // can't break the dispatch-history bullet list. Whitespace runs
        // collapsed; trimmed.
        std::string SanitizeForHistory(std::string s) {
            for (char& c : s) {
                if (c == '\n' || c == '\r' || c == '\t' || c == '|' || c == '`') c = ' ';
            }
            size_t start = s.find_first_not_of(' ');
            size_t end   = s.find_last_not_of(' ');
            if (start == std::string::npos) return {};
            return s.substr(start, end - start + 1);
        }
    }

    void NPCIndex::RecordStoryDispatch(const std::string& storyType,
                                        const std::string& subType,
                                        const std::string& npcName,
                                        const std::string& narration) {
        if (storyType.empty()) return;
        // NPC DM types are tracked elsewhere — keep this ring buffer Story-DM-only.
        if (storyType == "npc_interaction" || storyType == "npc_gossip") return;

        auto* cal = RE::Calendar::GetSingleton();
        float now = cal ? cal->GetCurrentGameTime() : 0.f;

        StoryDispatchEntry entry;
        entry.type      = SanitizeForHistory(storyType);
        entry.subType   = SanitizeForHistory(subType);
        entry.npcName   = SanitizeForHistory(npcName);
        entry.narration = SanitizeForHistory(narration);  // full text — emission truncates per prompt budget
        entry.gameTime  = now;
        entry.outcome   = DispatchOutcome::Dispatched;  // optimistic; flipped by MarkLastDispatchFailed on validation abort

        if (entry.type.empty()) return;

        std::unique_lock lock(m_mutex);
        m_recentDispatches.push_front(std::move(entry));
        while (static_cast<int>(m_recentDispatches.size()) > MAX_RECENT_DISPATCHES) {
            m_recentDispatches.pop_back();
        }
    }

    void NPCIndex::MarkLastDispatchFailed(const std::string& reason) {
        std::string clean = SanitizeForHistory(reason);
        if (clean.size() > 80) clean.resize(80);  // keep history block compact

        std::unique_lock lock(m_mutex);
        if (m_recentDispatches.empty()) return;
        auto& head = m_recentDispatches.front();
        head.outcome = DispatchOutcome::Rejected;
        head.reason  = std::move(clean);
    }

    void NPCIndex::SetRecentGossipContext(const std::string& gossipLines) {
        // Inject hold names into gossip lines by resolving NPC names to holds.
        // Single actor scan builds name→hold map, then applies to all lines.
        if (gossipLines.empty()) {
            std::unique_lock lock(m_mutex);
            m_recentGossipContext = "";
            return;
        }

        // Collect all NPC names from gossip lines first
        std::set<std::string> npcNamesLower;
        {
            std::istringstream scan(gossipLines);
            std::string line;
            while (std::getline(scan, line)) {
                if (line.find("[") != std::string::npos) continue;  // already tagged
                auto dashPos = line.find("- ");
                if (dashPos == std::string::npos) continue;
                auto toldPos = line.find(" told ", dashPos + 2);
                if (toldPos != std::string::npos) {
                    npcNamesLower.insert(StringUtils::ToLowerStd(
                        line.substr(dashPos + 2, toldPos - dashPos - 2)));
                }
            }
        }

        // Single actor scan: build name→hold map
        std::unordered_map<std::string, std::string> nameToHold;
        if (!npcNamesLower.empty()) {
            ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) -> bool {
                if (!actor) return false;
                auto* dispName = actor->GetDisplayFullName();
                if (!dispName) return false;
                std::string nameLower = StringUtils::ToLowerStd(dispName);
                if (npcNamesLower.count(nameLower)) {
                    nameToHold[nameLower] = GetNPCHoldName(actor);
                    // Stop early if all names resolved
                    if (nameToHold.size() == npcNamesLower.size()) return true;
                }
                return false;
            });
        }

        // Apply hold names to gossip lines
        std::string result;
        result.reserve(gossipLines.size() + 200);
        std::istringstream stream(gossipLines);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;
            if (line.find("[") != std::string::npos && line.find("]") != std::string::npos) {
                result += line + "\n";
                continue;
            }
            std::string holdName;
            auto dashPos = line.find("- ");
            if (dashPos != std::string::npos) {
                auto toldPos = line.find(" told ", dashPos + 2);
                if (toldPos != std::string::npos) {
                    std::string nameLower = StringUtils::ToLowerStd(
                        line.substr(dashPos + 2, toldPos - dashPos - 2));
                    auto it = nameToHold.find(nameLower);
                    if (it != nameToHold.end() && !it->second.empty()) {
                        holdName = it->second;
                    }
                }
            }
            if (!holdName.empty()) {
                result += "- [" + holdName + "] " + line.substr(2) + "\n";
            } else {
                result += line + "\n";
            }
        }

        std::unique_lock lock(m_mutex);
        m_recentGossipContext = result;
    }

    std::string NPCIndex::GetStoryTypeCountsMarkdown(const std::unordered_set<std::string>& relevantTypes) const {
        std::shared_lock lock(m_mutex);
        if (m_storyTypeCounts.empty()) return "";

        std::string result;
        bool first = true;
        for (const auto& [type, count] : m_storyTypeCounts) {
            if (!relevantTypes.empty() && relevantTypes.find(type) == relevantTypes.end()) continue;
            if (!first) result += ", ";
            result += type;
            result += ": ";
            result += std::to_string(count);
            first = false;
        }
        return result;
    }

    std::string NPCIndex::GetPreferredNPCType() const {
        std::shared_lock lock(m_mutex);
        int interactionN = 0, gossipN = 0;
        auto it1 = m_storyTypeCounts.find("npc_interaction");
        auto it2 = m_storyTypeCounts.find("npc_gossip");
        if (it1 != m_storyTypeCounts.end()) interactionN = it1->second;
        if (it2 != m_storyTypeCounts.end()) gossipN = it2->second;

        // Force underrepresented type when diff >= 2; no preference otherwise
        if (gossipN - interactionN >= 2) return "npc_interaction";
        if (interactionN - gossipN >= 2) return "npc_gossip";
        return "";
    }

    // =========================================================================
    // Quest Item / Victim Rotation Tracking
    // =========================================================================

    void NPCIndex::NotifyQuestItemUsed(const std::string& itemName) {
        if (itemName.empty()) return;
        std::unique_lock lock(m_mutex);
        // Avoid duplicate consecutive entries
        if (!m_recentQuestItems.empty() && m_recentQuestItems.back() == itemName) return;
        m_recentQuestItems.push_back(itemName);
        if (static_cast<int>(m_recentQuestItems.size()) > MAX_RECENT_QUEST_ITEMS) {
            m_recentQuestItems.pop_front();
        }
        logger::info("[StoryDM] Tracked quest item: '{}' (history: {})", itemName, m_recentQuestItems.size());
    }

    void NPCIndex::NotifyRescueVictimUsed(const std::string& victimName) {
        if (victimName.empty()) return;
        std::unique_lock lock(m_mutex);
        if (!m_recentRescueVictims.empty() && m_recentRescueVictims.back() == victimName) return;
        m_recentRescueVictims.push_back(victimName);
        if (static_cast<int>(m_recentRescueVictims.size()) > MAX_RECENT_RESCUE_VICTIMS) {
            m_recentRescueVictims.pop_front();
        }
        logger::info("[StoryDM] Tracked rescue victim: '{}' (history: {})", victimName, m_recentRescueVictims.size());
    }

    void NPCIndex::NotifyQuestLocationUsed(const std::string& locationName) {
        if (locationName.empty()) return;
        std::unique_lock lock(m_mutex);
        if (!m_recentQuestLocations.empty() && m_recentQuestLocations.back() == locationName) return;
        m_recentQuestLocations.push_back(locationName);
        if (static_cast<int>(m_recentQuestLocations.size()) > MAX_RECENT_QUEST_LOCATIONS) {
            m_recentQuestLocations.pop_front();
        }
        logger::info("[StoryDM] Tracked quest location: '{}' (history: {})", locationName, m_recentQuestLocations.size());
    }

    std::string NPCIndex::GetRecentQuestLocationsString() const {
        std::shared_lock lock(m_mutex);
        if (m_recentQuestLocations.empty()) return "";
        std::string result;
        for (size_t i = 0; i < m_recentQuestLocations.size(); ++i) {
            if (i > 0) result += ", ";
            result += m_recentQuestLocations[i];
        }
        return result;
    }

    std::string NPCIndex::GetRecentQuestItemsString() const {
        std::shared_lock lock(m_mutex);
        if (m_recentQuestItems.empty()) return "";
        std::string result;
        for (size_t i = 0; i < m_recentQuestItems.size(); ++i) {
            if (i > 0) result += ", ";
            result += m_recentQuestItems[i];
        }
        return result;
    }

    std::string NPCIndex::GetRecentRescueVictimsString() const {
        std::shared_lock lock(m_mutex);
        if (m_recentRescueVictims.empty()) return "";
        std::string result;
        for (size_t i = 0; i < m_recentRescueVictims.size(); ++i) {
            if (i > 0) result += ", ";
            result += m_recentRescueVictims[i];
        }
        return result;
    }

    std::unordered_set<std::string> NPCIndex::GetRecentQuestItemNames() const {
        std::shared_lock lock(m_mutex);
        std::unordered_set<std::string> result;
        for (const auto& name : m_recentQuestItems) {
            result.insert(StringUtils::ToLowerStd(name));
        }
        return result;
    }

    std::string NPCIndex::GetHouseholdString(RE::Actor* actor) {
        if (!actor) return "";

        auto* locResolver = LocationResolver::GetSingleton();
        auto householdFormIds = locResolver->GetHouseholdMembers(actor);
        if (householdFormIds.empty()) return "";

        std::string result;
        int count = 0;
        for (auto baseFormId : householdFormIds) {
            if (count >= 4) break;  // Cap at 4 housemates per candidate

            auto* baseForm = RE::TESForm::LookupByID<RE::TESNPC>(baseFormId);
            if (!baseForm) continue;

            auto name = baseForm->GetFullName();
            if (!name || name[0] == '\0') continue;

            // Skip dead or disabled household members — prevents DM from picking
            // dead NPCs as rescue victims
            auto* memberActor = GetSingleton()->FindByName(name);
            if (memberActor && (memberActor->IsDead() || memberActor->IsDisabled())) continue;

            if (count > 0) result += ", ";
            result += name;
            count++;
        }
        return result;
    }

    std::vector<RE::FormID> NPCIndex::GetDMCandidatePoolFormIDs() const {
        std::shared_lock lock(m_mutex);
        std::vector<RE::FormID> result;
        result.reserve(m_dmCandidatePool.size());
        for (const auto& [name, formId] : m_dmCandidatePool) {
            result.push_back(formId);
        }
        return result;
    }

    std::vector<RE::FormID> NPCIndex::GetNPCCandidatePoolFormIDs() const {
        std::shared_lock lock(m_mutex);
        std::vector<RE::FormID> result;
        result.reserve(m_npcCandidatePool.size());
        for (const auto& [name, formId] : m_npcCandidatePool) {
            result.push_back(formId);
        }
        return result;
    }

    float NPCIndex::GetStoryCooldownHours() {
        static auto* global = RE::TESForm::LookupByEditorID<RE::TESGlobal>("IntelEngine_StoryEngineCooldown");
        float mcmCooldown = global ? global->value : 24.0f;
        // Dispatched NPCs must stay out for at least the absence period too
        float absenceHours = Settings::GetSingleton()->storyMinAbsenceDays * 24.0f;
        return std::max(mcmCooldown, absenceHours);
    }

    std::string NPCIndex::ScanActorsWithPackages(const std::vector<RE::FormID>& packageFormIDs) {
        // Map FormIDs to package index (0-2 = travel, 3 = stalk, 4 = sandbox, 5 = sandbox near player)
        std::unordered_map<RE::FormID, int> pkgMap;
        for (int i = 0; i < static_cast<int>(packageFormIDs.size()); ++i) {
            if (packageFormIDs[i] != 0)
                pkgMap[packageFormIDs[i]] = i;
        }
        if (pkgMap.empty()) return "[]";

        static const char* PKG_LABELS[] = {
            "Travel (Walk)", "Travel (Jog)", "Travel (Run)",
            "Travel (Stalk)", "Sandbox", "Sandbox (Near Player)"
        };

        auto* player = RE::PlayerCharacter::GetSingleton();
        std::string json = "[";
        bool first = true;

        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!actor || actor == player) return false;
            if (actor->IsDead() || actor->IsDisabled()) return false;

            auto* currentPkg = actor->GetCurrentPackage();
            if (!currentPkg) return false;

            auto it = pkgMap.find(currentPkg->GetFormID());
            if (it != pkgMap.end()) {
                if (!first) json += ",";
                first = false;
                json += "{\"name\":\"";
                auto name = actor->GetDisplayFullName();
                std::string nameStr = name ? name : "Unknown";
                json += MemoryDB::EscapeJsonString(nameStr);
                json += "\",\"formId\":";
                json += std::to_string(actor->GetFormID());
                json += ",\"pkgType\":\"";
                int idx = it->second;
                json += (idx >= 0 && idx < 6) ? PKG_LABELS[idx] : "Unknown";
                json += "\"}";
            }
            return false;
        });

        json += "]";
        return json;
    }

    void NPCIndex::NotifyStoryCooldown(RE::FormID formId, float gameTime) {
        std::unique_lock lock(m_mutex);
        m_storyCooldowns[formId] = gameTime;

        // Prune expired entries when map grows large (use penalty window, not hard block)
        if (m_storyCooldowns.size() > 100) {
            float cooldownDays = GetStoryCooldownHours() / 24.0f;  // MCM-configurable
            auto* calendar = RE::Calendar::GetSingleton();
            if (calendar) {
                float now = calendar->GetCurrentGameTime();
                std::erase_if(m_storyCooldowns, [&](const auto& pair) {
                    return (now - pair.second) >= cooldownDays;
                });
            }
        }
    }

    bool NPCIndex::IsOnStoryCooldown(RE::FormID formId, float cooldownHours) const {
        std::shared_lock lock(m_mutex);
        auto it = m_storyCooldowns.find(formId);
        if (it == m_storyCooldowns.end()) return false;

        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return false;

        float currentGameTime = calendar->GetCurrentGameTime();
        float cooldownDays = cooldownHours / 24.0f;
        return (currentGameTime - it->second) < cooldownDays;
    }

    void NPCIndex::NotifySocialCooldown(RE::FormID formId, float gameTime, float cooldownHours) {
        std::unique_lock lock(m_mutex);
        m_socialCooldowns[formId] = gameTime;
        m_socialCooldownHours.store(cooldownHours, std::memory_order_relaxed);

        if (m_socialCooldowns.size() > 100) {
            float cooldownDays = cooldownHours / 24.0f;
            auto* calendar = RE::Calendar::GetSingleton();
            if (calendar) {
                float now = calendar->GetCurrentGameTime();
                std::erase_if(m_socialCooldowns, [&, cooldownDays](const auto& pair) {
                    return (now - pair.second) >= cooldownDays;
                });
            }
        }
    }

    bool NPCIndex::IsOnSocialCooldown(RE::FormID formId) const {
        std::shared_lock lock(m_mutex);
        auto it = m_socialCooldowns.find(formId);
        if (it == m_socialCooldowns.end()) return false;

        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar) return false;

        float currentGameTime = calendar->GetCurrentGameTime();
        float cooldownDays = m_socialCooldownHours.load(std::memory_order_relaxed) / 24.0f;
        return (currentGameTime - it->second) < cooldownDays;
    }

    float NPCIndex::GetSocialCooldownHours() const {
        return m_socialCooldownHours.load(std::memory_order_relaxed);
    }

}  // namespace IntelEngine
