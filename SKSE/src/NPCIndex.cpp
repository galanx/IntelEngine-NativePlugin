/**
 * NPC Index Implementation
 *
 * Builds and maintains an indexed database of all game NPCs.
 */

#include "NPCIndex.h"
#include <algorithm>
#include "CellAnalyzer.h"
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

    bool NPCIndex::IsPotentialFollower(RE::Actor* actor) {
        if (!actor) return false;
        static RE::TESFaction* s_potentialFollowerFaction = nullptr;
        if (!s_potentialFollowerFaction) {
            s_potentialFollowerFaction = RE::TESForm::LookupByID<RE::TESFaction>(0x0005C84D);
        }
        return s_potentialFollowerFaction && actor->IsInFaction(s_potentialFollowerFaction);
    }

    // ── Location Blocklist (populated from plugin config) ──
    static std::vector<std::string> s_blockedLocations;  // lowercase
    static std::mutex s_locationMutex;
    static std::chrono::steady_clock::time_point s_locationBlocklistLastRefresh;

    static void RefreshLocationBlocklist() {
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(s_locationMutex);
            if (now - s_locationBlocklistLastRefresh < std::chrono::seconds(30)) return;
            s_locationBlocklistLastRefresh = now;
        }

        if (!SkyrimNetAPI::GetPluginConfigValue) return;
        std::string csv = SkyrimNetAPI::GetPluginConfigValue(
            "IntelEngine", "story.location_blocklist", "");

        std::vector<std::string> newList;
        if (!csv.empty()) {
            std::stringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    newList.push_back(StringUtils::ToLowerStd(token.substr(start, end - start + 1)));
                }
            }
        }
        std::lock_guard<std::mutex> lock(s_locationMutex);
        s_blockedLocations = std::move(newList);
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

    // ── NPC Name Blocklist (populated from plugin config) ──
    static std::vector<std::string> s_blockedNPCNames;  // lowercase
    static std::mutex s_npcNameMutex;
    static std::chrono::steady_clock::time_point s_npcNameBlocklistLastRefresh;

    static void RefreshNPCNameBlocklist() {
        auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(s_npcNameMutex);
            if (now - s_npcNameBlocklistLastRefresh < std::chrono::seconds(30)) return;
            s_npcNameBlocklistLastRefresh = now;
        }

        if (!SkyrimNetAPI::GetPluginConfigValue) return;
        std::string csv = SkyrimNetAPI::GetPluginConfigValue(
            "IntelEngine", "story.npc_blocklist", "");

        std::vector<std::string> newList;
        if (!csv.empty()) {
            std::stringstream ss(csv);
            std::string token;
            while (std::getline(ss, token, ',')) {
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    newList.push_back(StringUtils::ToLowerStd(token.substr(start, end - start + 1)));
                }
            }
        }
        std::lock_guard<std::mutex> lock(s_npcNameMutex);
        s_blockedNPCNames = std::move(newList);
        if (!s_blockedNPCNames.empty()) {
            logger::info("NPC name blocklist refreshed: {} entries", s_blockedNPCNames.size());
        }
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
            "Hagraven", "Chaurus", "Horker", "Fox"
        };
        return kExcluded.count(name) > 0;
    }

    bool NPCIndex::IsEligibleStoryCandidate(RE::Actor* actor, RE::Actor* player,
        RE::TESObjectCELL* playerCell, SlotTracker* tracker) {
        if (!actor || actor == player) return false;
        if (actor->IsDeleted()) return false;
        if (!actor->GetParentCell()) return false;
        // Filter out nameless/unknown actors
        auto displayName = actor->GetDisplayFullName();
        if (!displayName || !displayName[0] || std::string_view(displayName) == "Unknown") return false;
        if (actor->IsDead() || actor->IsDisabled()) return false;
        if (actor->IsInCombat()) return false;
        if (actor->IsPlayerTeammate()) return false;
        if (actor->IsHostileToActor(player)) return false;
        if (actor->GetParentCell() == playerCell) return false;
        if (tracker && (tracker->HasActiveTask(actor) || tracker->IsOnCooldown(actor))) return false;

        // Filter out animals/creatures — only humanoid NPCs with ActorTypeNPC keyword (0x13794)
        static auto* kwActorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);
        if (kwActorTypeNPC && !actor->HasKeyword(kwActorTypeNPC)) return false;

        // Filter out child NPCs — children can't be story candidates
        if (auto* race = actor->GetRace()) {
            if (race->IsChildRace()) return false;
        }

        // Full MCM cooldown — prevents wasted LLM turns picking NPCs Papyrus would reject
        if (NPCIndex::GetSingleton()->IsOnStoryCooldown(actor->GetFormID(), GetStoryCooldownHours())) return false;

        // Plugin-configured faction blocklist (refreshes every 30s)
        RefreshFactionBlocklist();
        if (IsInBlockedFaction(actor)) return false;

        // Plugin-configured NPC name blocklist (refreshes every 30s)
        RefreshNPCNameBlocklist();
        if (IsBlockedByName(actor)) return false;

        // Danger zone candidate filtering (MCM-controlled)
        {
            int policy = NPCIndex::GetSingleton()->m_dangerZonePolicy.load(std::memory_order_relaxed);
            if (policy > 0 && CellAnalyzer::GetSingleton()->IsPlayerInDangerousLocation()) {
                if (policy == 3) return false;
                if (policy == 2 && !IsPotentialFollower(actor)) return false;
                if (policy == 1 && ClassifyNPCArchetype(actor) == "CIVILIAN") return false;
            }
        }

        // Player home candidate filtering (MCM-controlled)
        {
            int policy = NPCIndex::GetSingleton()->m_playerHomePolicy.load(std::memory_order_relaxed);
            if (policy > 0 && CellAnalyzer::GetSingleton()->IsPlayerInOwnHome()) {
                if (policy == 3) return false;
                if (policy == 2 && !IsPotentialFollower(actor)) return false;
                if (policy == 1 && ClassifyNPCArchetype(actor) == "CIVILIAN") return false;
            }
        }

        return true;
    }

    bool NPCIndex::IsEligibleStoryCandidateRelaxed(RE::Actor* actor, RE::Actor* player,
        SlotTracker* tracker) {
        if (!actor || actor == player) return false;

        // Filter out nameless/unknown actors (dynamically created NPCs with no display name)
        auto displayName = actor->GetDisplayFullName();
        if (!displayName || !displayName[0] || std::string_view(displayName) == "Unknown") {
            logger::debug("[StoryDM] Rejected 0x{:08X}: no display name", actor->GetFormID());
            return false;
        }

        if (actor->IsDead()) {
            logger::debug("[StoryDM] Rejected '{}': dead", displayName);
            return false;
        }
        if (actor->IsDisabled()) {
            logger::debug("[StoryDM] Rejected '{}': disabled", displayName);
            return false;
        }
        if (tracker && tracker->HasActiveTask(actor)) {
            logger::debug("[StoryDM] Rejected '{}': active task", displayName);
            return false;
        }
        if (tracker && tracker->IsOnCooldown(actor)) {
            logger::debug("[StoryDM] Rejected '{}': task cooldown", displayName);
            return false;
        }

        // Filter out animals/creatures — only humanoid NPCs with ActorTypeNPC keyword (0x13794)
        static auto* kwActorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);
        if (kwActorTypeNPC && !actor->HasKeyword(kwActorTypeNPC)) {
            logger::debug("[StoryDM] Rejected '{}': not ActorTypeNPC", displayName);
            return false;
        }

        // Filter out child NPCs
        if (auto* race = actor->GetRace()) {
            if (race->IsChildRace()) {
                logger::debug("[StoryDM] Rejected '{}': child race", displayName);
                return false;
            }
        }

        // Hard cooldown block — prevents recently-dispatched NPCs from appearing in
        // the pool entirely, forcing variety. Matches IsEligibleStoryCandidate behavior.
        if (GetSingleton()->IsOnStoryCooldown(actor->GetFormID(), GetStoryCooldownHours())) {
            logger::debug("[StoryDM] Rejected '{}': story cooldown", displayName);
            return false;
        }

        // Plugin-configured faction blocklist (shared with IsEligibleStoryCandidate)
        RefreshFactionBlocklist();
        if (IsInBlockedFaction(actor)) {
            logger::debug("[StoryDM] Rejected '{}': blocked faction", displayName);
            return false;
        }

        // Plugin-configured NPC name blocklist (shared with IsEligibleStoryCandidate)
        RefreshNPCNameBlocklist();
        if (IsBlockedByName(actor)) {
            logger::debug("[StoryDM] Rejected '{}': blocked by name", displayName);
            return false;
        }

        // Danger zone candidate filtering (MCM-controlled)
        {
            int policy = NPCIndex::GetSingleton()->m_dangerZonePolicy.load(std::memory_order_relaxed);
            if (policy > 0 && CellAnalyzer::GetSingleton()->IsPlayerInDangerousLocation()) {
                if (policy == 3) {
                    logger::debug("[StoryDM] Rejected '{}': danger zone (block all)", displayName);
                    return false;
                }
                if (policy == 2 && !IsPotentialFollower(actor)) {
                    logger::debug("[StoryDM] Rejected '{}': danger zone (followers only)", displayName);
                    return false;
                }
                if (policy == 1 && ClassifyNPCArchetype(actor) == "CIVILIAN") {
                    logger::debug("[StoryDM] Rejected '{}': danger zone (civilian)", displayName);
                    return false;
                }
            }
        }

        // Player home candidate filtering (MCM-controlled)
        {
            int policy = NPCIndex::GetSingleton()->m_playerHomePolicy.load(std::memory_order_relaxed);
            if (policy > 0 && CellAnalyzer::GetSingleton()->IsPlayerInOwnHome()) {
                if (policy == 3) {
                    logger::debug("[StoryDM] Rejected '{}': player home (block all)", displayName);
                    return false;
                }
                if (policy == 2 && !IsPotentialFollower(actor)) {
                    logger::debug("[StoryDM] Rejected '{}': player home (followers only)", displayName);
                    return false;
                }
                if (policy == 1 && ClassifyNPCArchetype(actor) == "CIVILIAN") {
                    logger::debug("[StoryDM] Rejected '{}': player home (civilian)", displayName);
                    return false;
                }
            }
        }

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

    std::string NPCIndex::GetNPCHoldName(RE::Actor* actor) {
        if (!actor) return "";

        RE::BGSLocation* loc = nullptr;

        // Loaded: get location from parent cell
        if (auto* cell = actor->GetParentCell()) {
            loc = cell->GetLocation();
        }

        // Unloaded: fall back to editor location
        if (!loc) {
            loc = actor->GetEditorLocation();
        }

        if (!loc) return "";

        // Walk to topmost named parent (the hold)
        while (loc->parentLoc) {
            loc = loc->parentLoc;
        }
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
            return actor && actor != sender && actor != player &&
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

        // Phases 3+4: Single loaded-actor scan for guards AND civilians
        std::vector<RE::Actor*> guards;
        std::vector<RE::Actor*> sameHoldCivilians;
        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!isValidMessenger(actor)) return false;
            std::string archetype = ClassifyNPCArchetype(actor);
            if (archetype == "GUARD" && !senderHold.empty() && GetNPCHoldName(actor) == senderHold) {
                guards.push_back(actor);
            } else if (archetype == "CIVILIAN" && !senderHold.empty() && GetNPCHoldName(actor) == senderHold) {
                sameHoldCivilians.push_back(actor);
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

        std::vector<const char*> types;

        // seek_player: civilians can't enter danger zones
        if (!(isCivilian && dangerous))
            types.push_back("seek_player");

        // informant: never in danger zones (gossip isn't worth risking your life)
        if (!dangerous)
            types.push_back("informant");

        // road_encounter: exterior only
        if (!interior)
            types.push_back("road_encounter");

        // ambush: combat-capable only
        if (!isCivilian)
            types.push_back("ambush");

        // stalker: exterior only, not civilian (needs stealth capability)
        if (!interior && !isCivilian)
            types.push_back("stalker");

        // message: anyone can send a message (messenger selection is automatic)
        types.push_back("message");

        // quest: anyone can give a quest (civilians ask for help, warriors offer jobs)
        types.push_back("quest");

        std::string result;
        for (size_t i = 0; i < types.size(); ++i) {
            if (i > 0) result += ", ";
            result += types[i];
        }
        return result;
    }

    std::string NPCIndex::GetNPCBioLine(RE::Actor* actor) {
        if (!actor) return "";

        // Prefer SkyrimNet bio summary — rich, personality-aware, faction-aware
        auto* memDB = MemoryDB::GetSingleton();
        if (memDB) {
            std::string bio = memDB->GetNPCBioSummary(actor->GetFormID());
            if (!bio.empty()) return bio;
        }

        // Fallback: race + vanilla factions from game data (for NPCs without bio files)
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

    std::string NPCIndex::BuildDungeonMasterContext(int maxCandidates, float absenceDays) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return "";
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return "";

        // Block all dispatches when player is at a blocklisted location
        if (IsPlayerInBlockedLocation()) {
            logger::info("[StoryDM] Player at blocked location — skipping DM tick");
            return "";
        }

        auto* tracker = SlotTracker::GetSingleton();
        auto* memDB = MemoryDB::GetSingleton();
        auto* locResolver = LocationResolver::GetSingleton();
        auto* cellAnalyzer = CellAnalyzer::GetSingleton();
        auto* settings = Settings::GetSingleton();

        // --- Gather eligible candidates ---
        float absenceHours = absenceDays * 24.0f;
        // Name-based exclusion (lowercase) — handles dual UUID + stale FormID bugs
        auto recentPlayerNPCs = memDB->GetRecentPlayerInteractionNames(absenceHours);

        // dbFormId: the FormID from MemoryDB (may differ from actor->GetFormID() if stale).
        // Used for MemoryDB queries (memories, events) since the UUID cache maps DB FormIDs.
        // actor->GetFormID() is used for game-engine checks (cell, combat, etc).
        struct CandidateInfo { RE::Actor* actor; float score; RE::FormID dbFormId; bool fromMemoryDB; };
        std::vector<CandidateInfo> pool;

        // Memory-ranked candidates first — relaxed check allows unloaded NPCs
        // (dispatch system handles bringing them into the world via MoveTo + off-screen tracking)
        // NOTE: No collectTarget cap — process ALL ranked candidates from MemoryDB.
        // The DB query already limits results (maxCandidates * 4). Eligibility checks are cheap.
        // Scoring + final trim to maxCandidates handles pool sizing. A collectTarget cap was
        // cutting off high-value NPCs at positions 11+ before they could be scored.
        auto ranked = memDB->GetRankedCandidateFormIDs(maxCandidates * 4);
        // Log full ranked list for diagnostics (helps identify missing NPCs)
        for (int ri = 0; ri < static_cast<int>(ranked.size()); ++ri) {
            logger::debug("[StoryDM] DB rank #{}: '{}' (0x{:08X}, dbScore={:.1f})",
                ri + 1, ranked[ri].name, ranked[ri].formId, ranked[ri].score);
        }
        int dbUnresolved = 0, dbIneligible = 0, dbPlayerCell = 0, dbAccepted = 0, dbCreature = 0;
        for (const auto& [formId, name, score] : ranked) {
            // Pre-filter generic creature/animal names before expensive resolve+scoring
            if (IsGenericCreatureName(name)) {
                dbCreature++;
                continue;
            }
            // Resolve via FormID first, fall back to name if FormID is stale
            auto* actor = ResolveFromMemoryDB(formId, name);
            if (!actor) {
                logger::warn("[StoryDM] Ranked '{}' (0x{:08X}, score={:.1f}) failed to resolve",
                    name, formId, score);
                dbUnresolved++;
                continue;
            }
            if (!IsEligibleStoryCandidateRelaxed(actor, player, tracker)) {
                logger::debug("[StoryDM] Ranked '{}' failed eligibility", name);
                dbIneligible++;
                continue;
            }
            // Skip if in same cell as player (already nearby — not interesting for story dispatch)
            if (actor->GetParentCell() && actor->GetParentCell() == playerCell) {
                dbPlayerCell++;
                continue;
            }
            pool.push_back({actor, score, formId, true});  // formId = DB's FormID for MemoryDB queries
            dbAccepted++;
        }
        logger::info("[StoryDM] MemoryDB candidates: {} ranked, {} accepted, "
            "{} creature, {} unresolved, {} ineligible, {} player-cell",
            ranked.size(), dbAccepted, dbCreature, dbUnresolved, dbIneligible, dbPlayerCell);

        // Always add random encounter NPCs for variety.
        // Even when MemoryDB pool is full, include random loaded NPCs so the LLM
        // can choose between history-driven stories and fresh encounters.
        constexpr int RANDOM_ENCOUNTER_SLOTS = 3;
        {
            std::unordered_set<RE::FormID> poolIds;
            for (auto& c : pool) poolIds.insert(c.actor->GetFormID());

            int randomsAdded = 0;
            int randomTarget = RANDOM_ENCOUNTER_SLOTS;

            ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
                if (randomsAdded >= randomTarget) return true;
                if (poolIds.count(actor->GetFormID())) return false;
                if (recentPlayerNPCs.count(StringUtils::ToLowerStd(actor->GetDisplayFullName()))) return false;
                if (!IsEligibleStoryCandidate(actor, player, playerCell, tracker)) return false;
                pool.push_back({actor, 0.0f, actor->GetFormID(), false});
                poolIds.insert(actor->GetFormID());
                randomsAdded++;
                return false;
            });
        }

        // Phase 3: Location-mate candidates for npc_interaction/npc_gossip variety.
        // Scans loaded actors sharing a location with existing pool members.
        // This is NOT player-proximity-biased like MemoryDB social data — it surfaces
        // natural NPC pairs (e.g., Carlotta/Mikael in Whiterun market) even without
        // prior recorded NPC-to-NPC events. NPCs with MemoryDB social history get boosted.
        {
            std::unordered_set<RE::FormID> poolIds;
            for (auto& c : pool) poolIds.insert(c.actor->GetFormID());

            // Collect locations of existing pool members (only loaded ones have locations)
            std::unordered_set<std::string> poolLocations;
            for (auto& c : pool) {
                std::string loc = GetNPCLocationName(c.actor);
                if (!loc.empty() && loc != "Unknown") {
                    poolLocations.insert(StringUtils::ToLowerStd(loc));
                }
            }

            // Build social score lookup from MemoryDB (for boosting location-mates with history)
            std::unordered_map<std::string, float> socialScoreLookup;  // lowercase name -> score
            auto socialNPCs = memDB->GetSociallyActiveFormIDs(20);
            for (const auto& [formId, name, socialScore] : socialNPCs) {
                socialScoreLookup[StringUtils::ToLowerStd(name)] = socialScore;
            }

            // Scan loaded actors for location-mates of pool members
            int locationMatesAdded = 0;
            constexpr int MAX_LOCATION_MATES = 4;
            constexpr float SOCIAL_MEMORY_BOOST = 0.3f;  // Multiplier for MemoryDB social score

            ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
                if (locationMatesAdded >= MAX_LOCATION_MATES) return true;
                if (!actor || poolIds.count(actor->GetFormID())) return false;
                std::string actorNameLower = StringUtils::ToLowerStd(actor->GetDisplayFullName());
                if (recentPlayerNPCs.count(actorNameLower)) return false;
                if (!IsEligibleStoryCandidate(actor, player, playerCell, tracker)) return false;

                // Check if this actor shares a location with any pool member
                std::string actorLoc = GetNPCLocationName(actor);
                if (actorLoc.empty()) return false;
                if (!poolLocations.count(StringUtils::ToLowerStd(actorLoc))) return false;

                // Base score: small constant. Boost if NPC has MemoryDB social activity.
                float score = 1.0f;
                auto socialIt = socialScoreLookup.find(actorNameLower);
                if (socialIt != socialScoreLookup.end()) {
                    score += socialIt->second * SOCIAL_MEMORY_BOOST;
                }

                pool.push_back({actor, score, actor->GetFormID(), false});
                poolIds.insert(actor->GetFormID());
                locationMatesAdded++;
                return false;
            });

            if (locationMatesAdded > 0) {
                logger::debug("[StoryDM] Added {} location-mate NPCs to pool", locationMatesAdded);
            }
        }

        if (pool.empty()) {
            logger::debug("[StoryDM] No eligible candidates for DM context");
            return "";
        }

        // --- Multi-factor scoring ---
        {
            // Factor 2: Absence bonus (old friends who haven't been seen in a long time)
            // relMap keyed by lowercase name — handles stale FormIDs in relationship data
            auto relationships = memDB->GetPlayerRelationshipData();
            std::unordered_map<std::string, PlayerRelationship> relMap;
            for (const auto& rel : relationships) {
                relMap[StringUtils::ToLowerStd(rel.name)] = rel;
            }

            // Live game time in seconds (same scale as DB game_time: days * 86400)
            float currentHours = memDB->GetCurrentDBHours();
            constexpr float ABSENCE_BONUS_WEIGHT = 1.5f;
            constexpr float GEOGRAPHIC_BONUS = 3.0f;
            constexpr float FRIEND_OF_FRIEND_BONUS = 2.0f;
            constexpr float NOVELTY_BONUS = 4.0f;
            constexpr float RANDOM_NOISE_MAX = 3.0f;

            // Factor 4: Geographic hold bonus — player's hold
            std::string playerHold = GetNPCHoldName(player);

            // Factor 5: Friend-of-friend — build second-degree connection set (by lowercase name)
            std::unordered_set<std::string> friendOfFriendNames;
            {
                // Sort player relationships by interaction count, take top 5
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

            // Factor 3: Random perturbation
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> noiseDist(0.0f, RANDOM_NOISE_MAX);

            for (auto& c : pool) {
                // Dampen MemoryDB activity to prevent feedback loops —
                // NPCs who get dispatched generate more events, raising their score.
                // log2 flattens the curve: 90→6.5, 10→3.5, 2→1.6, 0→0.
                float activity = std::log2(c.score + 1.0f);
                float absenceBonus = 0.0f;
                float geoBonus = 0.0f;
                float fofBonus = 0.0f;

                // Absence bonus — look up by lowercase name (survives FormID changes)
                std::string actorNameLower = StringUtils::ToLowerStd(c.actor->GetDisplayFullName());
                auto relIt = relMap.find(actorNameLower);
                if (relIt != relMap.end() && relIt->second.interactionCount > 0) {
                    // DB game_time is in seconds: divide by 86400 (3600*24) to get days
                    float daysSinceLast = (currentHours - relIt->second.lastInteractionHours) / 86400.0f;
                    if (daysSinceLast < 0.0f) daysSinceLast = 0.0f;
                    float absenceRatio = std::min(daysSinceLast / 30.0f, 2.0f);
                    // Depth from interaction count. sqrt() dampens: 100→10, 25→5, 4→2.
                    // Capped at 15.0 (225+ interactions) to prevent extreme values from
                    // permanently locking rankings, while still strongly rewarding deep history.
                    float depth = std::min(std::sqrt(static_cast<float>(relIt->second.interactionCount)), 15.0f);
                    absenceBonus = absenceRatio * depth * ABSENCE_BONUS_WEIGHT;
                }

                // Novelty bonus — graduated decay by interaction count.
                // Unknown NPCs (0 interactions) get full bonus (4.0).
                // Shallow history decays smoothly: 1→2.0, 3→1.0, 10→0.36.
                // Deep history (~0) lets absence bonus dominate instead.
                // Hyperbolic curve fills the dead zone between novelty and absence.
                float noveltyBonus = 0.0f;
                if (relIt == relMap.end()) {
                    noveltyBonus = NOVELTY_BONUS;
                } else {
                    noveltyBonus = NOVELTY_BONUS / (1.0f + static_cast<float>(relIt->second.interactionCount));
                }

                // Geographic bonus — same hold as player (uses editor location for unloaded)
                if (!playerHold.empty()) {
                    std::string npcHold = GetNPCHoldName(c.actor);
                    if (!npcHold.empty() && npcHold == playerHold) {
                        geoBonus = GEOGRAPHIC_BONUS;
                    }
                }

                // Friend-of-friend bonus — look up by lowercase name
                if (friendOfFriendNames.count(actorNameLower)) {
                    fofBonus = FRIEND_OF_FRIEND_BONUS;
                }

                // Hard cooldown block is now in eligibility checks — NPCs on cooldown
                // never enter the pool, so no scoring penalty needed.

                float noise = noiseDist(rng);
                c.score = activity + absenceBonus + noveltyBonus + geoBonus + fofBonus + noise;

                if (absenceBonus > 1.0f || geoBonus > 0.0f || fofBonus > 0.0f || noveltyBonus > 0.0f) {
                    logger::debug("[StoryDM] Score: {} = {:.1f} (act={:.1f} abs={:.1f} nov={:.1f} geo={:.1f} fof={:.1f} rng={:.1f})",
                        c.actor->GetDisplayFullName(), c.score, activity, absenceBonus, noveltyBonus, geoBonus, fofBonus, noise);
                }
            }

            // Re-sort by enhanced score
            std::sort(pool.begin(), pool.end(), [](const CandidateInfo& a, const CandidateInfo& b) {
                return a.score > b.score;
            });

            // Split pool: top maxCandidates history-driven + guaranteed random encounters.
            // MemoryDB NPCs always outscore randoms, so we separate them before trimming
            // to guarantee the LLM sees both familiar faces AND fresh encounters.
            std::vector<CandidateInfo> rankedPool;
            std::vector<CandidateInfo> randomPool;
            for (auto& c : pool) {
                if (c.fromMemoryDB) {
                    rankedPool.push_back(c);
                } else {
                    randomPool.push_back(c);
                }
            }
            // Trim ranked to maxCandidates, then append up to 3 random encounters
            if (static_cast<int>(rankedPool.size()) > maxCandidates) {
                rankedPool.resize(maxCandidates);
            }
            if (static_cast<int>(randomPool.size()) > RANDOM_ENCOUNTER_SLOTS) {
                randomPool.resize(RANDOM_ENCOUNTER_SLOTS);
            }
            pool = rankedPool;
            for (auto& c : randomPool) {
                pool.push_back(c);
            }
        }

        // Store DM candidate pool for exact name->FormID resolution during dispatch.
        // Separate from NPC pool — clearing this won't affect NPC tick's async response.
        {
            std::unique_lock poolLock(m_mutex);
            m_dmCandidatePool.clear();
            for (const auto& c : pool) {
                std::string nameLower = StringUtils::ToLowerStd(c.actor->GetDisplayFullName());
                m_dmCandidatePool[nameLower] = c.actor->GetFormID();
            }
        }

        // --- Build markdown ---
        std::string md;
        md.reserve(2048);

        // Player location
        std::string playerLoc = locResolver->GetActorLocationName(player).c_str();
        if (playerLoc.empty()) playerLoc = "Unknown";

        // Danger + environment
        bool dangerous = cellAnalyzer->IsPlayerInDangerousLocation();
        bool interior = playerCell->IsInteriorCell();

        // Hold name (parent location)
        std::string holdName;
        if (auto* loc = player->GetCurrentLocation()) {
            if (auto* parent = loc->parentLoc) {
                auto n = parent->GetFullName();
                if (n && n[0]) holdName = n;
            }
            if (holdName.empty()) {
                auto n = loc->GetFullName();
                if (n && n[0]) holdName = n;
            }
        }
        if (holdName.empty()) holdName = "Unknown";

        const char* timeStr = GetTimeOfDayString();

        md += "## World State\n";
        md += "- Player: ";  md += player->GetDisplayFullName();
        md += " at ";        md += playerLoc;        md += "\n";
        md += "- Danger: ";  md += dangerous ? "DANGEROUS" : "SAFE";  md += "\n";
        md += "- Environment: ";  md += interior ? "Interior" : "Exterior";  md += "\n";
        md += "- Hold: ";    md += holdName;   md += "\n";
        md += "- Time: ";    md += timeStr;    md += "\n\n";

        // --- Candidate pool (ascending score: most important candidates last for LLM attention) ---
        std::reverse(pool.begin(), pool.end());
        md += "## Candidate Pool\n\n";
        int memPerCandidate = std::min(settings->maxMemoriesInContext, 2);

        for (int i = 0; i < static_cast<int>(pool.size()); ++i) {
            auto* actor = pool[i].actor;

            std::string name = actor->GetDisplayFullName();
            std::string archetype = ClassifyNPCArchetype(actor);
            std::string loc = GetNPCLocationName(actor);
            if (loc.empty()) loc = "Unknown";

            // Gender from actor base (prevents LLM guessing wrong from names)
            const char* gender = "Male";
            if (auto* npc = actor->GetActorBase()) {
                if (npc->GetSex() == RE::SEX::kFemale) gender = "Female";
            }

            char uuid[16];
            snprintf(uuid, sizeof(uuid), "0x%08X", actor->GetFormID());

            std::string bio = GetNPCBioLine(actor);
            std::string eligibleTypes = GetEligibleStoryTypes(actor, archetype, dangerous, interior);

            md += "### ";  md += std::to_string(i + 1);  md += ". ";  md += name;
            md += " [";    md += archetype;  md += ", ";  md += gender;  md += "] - ";  md += loc;
            md += " (";    md += uuid;       md += ")\n";
            md += "Eligible: ";  md += eligibleTypes;  md += "\n";
            if (!bio.empty()) {
                md += "Bio: ";  md += bio;  md += "\n";
            }

            // Bio relationships — canonical connections from character prompt file
            // (family, faction members, friends/rivals — authored, not event-based)
            auto bioRels = memDB->GetNPCBioRelationships(actor->GetFormID());
            if (!bioRels.empty()) {
                md += "Relationships:\n";  md += bioRels;  md += "\n";
            }

            // Use dbFormId for MemoryDB queries — the DB's FormID is in the UUID cache.
            // actor->GetFormID() may differ if resolved via name (stale FormID workaround).
            RE::FormID queryFormId = pool[i].dbFormId;
            auto memories = memDB->GetFormattedMemories(queryFormId, memPerCandidate);
            if (!memories.empty()) {
                md += "Memories:\n";  md += memories;  md += "\n";
            }

            // Recent dialogue with player — gives DM context on what was already discussed
            auto dialogue = memDB->GetRecentDialogueForActor(queryFormId, 3);
            if (!dialogue.empty()) {
                md += "Last conversation:\n";  md += dialogue;  md += "\n";
            }

            // Recent events — surfaces NPC-to-NPC interactions and player encounters
            auto recentEvents = memDB->GetRecentEventsForActor(queryFormId, 3);
            if (!recentEvents.empty()) {
                md += "Recent:\n";  md += recentEvents;  md += "\n";
            }

            // In-game connections — NPCs with shared event history from MemoryDB.
            // Supplements bio relationships with dynamic in-game interactions.
            auto relatedNPCs = memDB->GetRelatedCandidateFormIDs(queryFormId, 5);
            if (!relatedNPCs.empty()) {
                md += "In-game connections: ";
                bool firstRel = true;
                for (const auto& rel : relatedNPCs) {
                    if (rel.formId == queryFormId) continue;  // skip self
                    if (!firstRel) md += ", ";
                    md += rel.name;
                    firstRel = false;
                }
                md += "\n";
            }

            md += "\n";
        }

        // Story type usage stats for DM balancing (only types this prompt can pick)
        static const std::unordered_set<std::string> dmTypes = {
            "seek_player", "informant", "road_encounter", "ambush", "stalker", "message", "quest"
        };
        auto typeCounts = GetStoryTypeCountsMarkdown(dmTypes);
        if (!typeCounts.empty()) {
            md += "## Story Type Picks This Session\n";
            md += typeCounts;
            md += "\n\n";
        }

        // Rotation hints — recently used quest items, rescue victims, and locations
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

        logger::info("[StoryDM] DM context: {} candidates, {} chars",
                     pool.size(), md.size());
        return MemoryDB::EscapeJsonString(md);
    }

    std::string NPCIndex::BuildNPCInteractionContext(int maxPairs) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return "";
        auto* playerCell = player->GetParentCell();
        if (!playerCell) return "";

        auto* tracker = SlotTracker::GetSingleton();
        auto* memDB = MemoryDB::GetSingleton();
        auto* locResolver = LocationResolver::GetSingleton();

        // Build social score lookup from MemoryDB (for boosting groups with history)
        std::unordered_map<std::string, float> socialScoreLookup;
        auto socialNPCs = memDB->GetSociallyActiveFormIDs(20);
        for (const auto& [formId, name, score] : socialNPCs) {
            socialScoreLookup[StringUtils::ToLowerStd(name)] = score;
        }

        // Collect eligible NPCs grouped by location.
        // Unlike BuildDungeonMasterContext, does NOT exclude player cell —
        // NPC-to-NPC interactions CAN happen with the player as witness.
        struct NPCEntry {
            RE::Actor* actor;
            std::string location;
            float socialScore;
        };
        std::unordered_map<std::string, std::vector<NPCEntry>> locationGroups;

        static auto* kwActorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(0x00013794);

        ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
            if (!actor || actor == player) return false;
            if (!actor->GetParentCell()) return false;
            if (actor->IsDead() || actor->IsDisabled()) return false;
            if (actor->IsInCombat()) return false;
            if (actor->IsHostileToActor(player)) return false;
            if (tracker && (tracker->HasActiveTask(actor) || tracker->IsOnCooldown(actor))) return false;
            if (kwActorTypeNPC && !actor->HasKeyword(kwActorTypeNPC)) return false;
            // Filter out child NPCs
            if (auto* race = actor->GetRace()) {
                if (race->IsChildRace()) return false;
            }
            // Full MCM cooldown — prevents wasted LLM turns
            if (IsOnStoryCooldown(actor->GetFormID(), GetStoryCooldownHours())) return false;
            // Social cooldown — prevents LLM picking pairs that Papyrus will reject
            if (IsOnSocialCooldown(actor->GetFormID())) return false;

            std::string loc = GetNPCLocationName(actor);
            if (loc.empty()) return false;

            std::string locLower = StringUtils::ToLowerStd(loc);
            std::string nameLower = StringUtils::ToLowerStd(actor->GetDisplayFullName());

            float socialScore = 0.0f;
            auto it = socialScoreLookup.find(nameLower);
            if (it != socialScoreLookup.end()) {
                socialScore = it->second;
            }

            locationGroups[locLower].push_back({actor, loc, socialScore});
            return false;
        });

        // Score and filter: only locations with 2+ NPCs form candidate groups
        struct ScoredGroup {
            std::string locationDisplay;
            std::vector<NPCEntry> npcs;
            float groupScore;
            bool playerNearby;
        };
        std::vector<ScoredGroup> groups;

        static thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> noiseDist(0.0f, 1.0f);

        for (auto& [locKey, npcs] : locationGroups) {
            if (npcs.size() < 2) continue;

            float groupScore = static_cast<float>(npcs.size());
            for (const auto& npc : npcs) {
                groupScore += npc.socialScore * 0.3f;
            }
            groupScore += noiseDist(rng);

            bool nearPlayer = false;
            for (const auto& npc : npcs) {
                if (npc.actor->GetParentCell() == playerCell) {
                    nearPlayer = true;
                    break;
                }
            }
            // Moderate boost for player-nearby groups — witnessing interactions
            // is more immersive than reading about them in bios later
            if (nearPlayer) {
                groupScore += 2.0f;
            }

            groups.push_back({npcs[0].location, std::move(npcs), groupScore, nearPlayer});
        }

        if (groups.empty()) {
            logger::debug("[NPCTick] No location groups with 2+ NPCs");
            return "";
        }

        // Sort by score, take top maxPairs
        std::sort(groups.begin(), groups.end(), [](const ScoredGroup& a, const ScoredGroup& b) {
            return a.groupScore > b.groupScore;
        });
        if (static_cast<int>(groups.size()) > maxPairs) {
            groups.resize(maxPairs);
        }

        // Store NPC interaction candidates for exact name->FormID resolution.
        // Separate from DM pool — clearing this won't affect DM tick's async response.
        {
            std::unique_lock poolLock(m_mutex);
            m_npcCandidatePool.clear();
            for (const auto& group : groups) {
                for (const auto& npc : group.npcs) {
                    std::string nameLower = StringUtils::ToLowerStd(npc.actor->GetDisplayFullName());
                    m_npcCandidatePool[nameLower] = npc.actor->GetFormID();
                }
            }
        }

        // --- Build markdown ---
        std::string md;
        md.reserve(1024);

        // World state (lighter than player-centric DM context)
        std::string playerLoc = locResolver->GetActorLocationName(player).c_str();
        if (playerLoc.empty()) playerLoc = "Unknown";

        const char* timeStr = GetTimeOfDayString();

        md += "## World State\n";
        md += "- Player: ";  md += player->GetDisplayFullName();
        md += " at ";        md += playerLoc;  md += "\n";
        md += "- Time: ";    md += timeStr;    md += "\n\n";

        md += "## NPC Groups by Location\n";

        for (const auto& group : groups) {
            md += "### ";  md += group.locationDisplay;
            md += " (player nearby: ";  md += group.playerNearby ? "yes" : "no";  md += ")\n";

            int npcCount = std::min(static_cast<int>(group.npcs.size()), 3);
            for (int i = 0; i < npcCount; ++i) {
                auto* actor = group.npcs[i].actor;
                std::string name = actor->GetDisplayFullName();
                std::string archetype = ClassifyNPCArchetype(actor);
                const char* gender = "Male";
                if (auto* npc = actor->GetActorBase()) {
                    if (npc->GetSex() == RE::SEX::kFemale) gender = "Female";
                }

                std::string bio = GetNPCBioLine(actor);

                md += "- ";   md += name;
                md += " [";   md += archetype;  md += ", ";  md += gender;  md += "]";
                if (actor->IsPlayerTeammate()) {
                    md += " (follower)";
                }
                if (!bio.empty()) {
                    md += " {";  md += bio;  md += "}";
                }

                auto memories = memDB->GetFormattedMemories(actor->GetFormID(), 2);
                if (!memories.empty()) {
                    md += ": ";  md += memories;
                }
                md += "\n";
            }
            md += "\n";
        }

        // Story type usage stats (informational only — balancing handled by preferredType)
        static const std::unordered_set<std::string> npcTypes = {
            "npc_interaction", "npc_gossip"
        };
        auto typeCounts = GetStoryTypeCountsMarkdown(npcTypes);
        if (!typeCounts.empty()) {
            md += "## Story Type Picks This Session\n";
            md += typeCounts;
            md += "\n\n";
        }

        logger::info("[NPCTick] NPC context: {} groups, {} chars", groups.size(), md.size());
        return MemoryDB::EscapeJsonString(md);
    }

    void NPCIndex::NotifyStoryTypePicked(const std::string& storyType) {
        std::unique_lock lock(m_mutex);
        m_storyTypeCounts[storyType]++;
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
                for (auto& c : nameStr) { if (c == '"') c = '\''; }
                json += nameStr;
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
