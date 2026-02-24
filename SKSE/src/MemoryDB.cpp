/**
 * MemoryDB - SkyrimNet Data API Client
 *
 * All data flows through SkyrimNet's PublicAPI (dllexport functions).
 * No direct SQLite access — SkyrimNet owns the DB connection.
 * IntelEngine owns scoring logic, formatting, and prompt assembly.
 */

#include "MemoryDB.h"
#include "SkyrimNetAPI.h"
#include "Settings.h"
#include "StringUtils.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace IntelEngine {

    // SkyrimNet stores game_time as GameDaysPassed * 86400 (game-seconds since epoch).
    // All our thresholds are written in hours — multiply by this to match DB units.
    constexpr float SECONDS_PER_HOUR = 3600.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void MemoryDB::InitializeAPI() {
        SkyrimNetAPI::Initialize();
    }

    bool MemoryDB::IsConnected() const {
        if (!SkyrimNetAPI::IsMemorySystemReady) return false;
        try {
            return SkyrimNetAPI::IsMemorySystemReady();
        } catch (...) {
            return false;
        }
    }

    void MemoryDB::ClearCaches() {
        std::lock_guard lock(m_mutex);
        m_bioSummaryCache.clear();
        m_cachedCurrentTime = 0.0f;
        logger::info("MemoryDB: Caches cleared");
    }

    // =========================================================================
    // GetNPCBioSummary (disk I/O — unchanged, uses SkyrimNet API for template name)
    // =========================================================================

    std::string MemoryDB::GetNPCBioSummary(RE::FormID formId) {
        // Check cache first (includes negative cache — empty string = no bio file)
        {
            std::lock_guard lock(m_mutex);
            auto cacheIt = m_bioSummaryCache.find(formId);
            if (cacheIt != m_bioSummaryCache.end()) {
                return cacheIt->second;
            }
        }

        // Get bio template name via SkyrimNet API
        std::string bioTemplate;
        if (SkyrimNetAPI::GetBioTemplateName) {
            try {
                bioTemplate = SkyrimNetAPI::GetBioTemplateName(formId);
            } catch (...) {
                logger::warn("MemoryDB: GetBioTemplateName exception for FormID 0x{:08X}", formId);
            }
        }

        if (bioTemplate.empty()) {
            std::lock_guard lock(m_mutex);
            m_bioSummaryCache[formId] = "";
            return "";
        }

        // Read the .prompt file from SkyrimNet's character prompts directory.
        const std::string promptFile = bioTemplate + ".prompt";
        std::string filePath;
        std::error_code ec;

        std::string dynamicPath = "Data/SKSE/Plugins/SkyrimNet/prompts/characters/" + promptFile;
        std::string originalPath = "Data/SKSE/Plugins/SkyrimNet/original_prompts/characters/" + promptFile;

        if (std::filesystem::exists(dynamicPath, ec)) {
            filePath = dynamicPath;
        } else if (std::filesystem::exists(originalPath, ec)) {
            filePath = originalPath;
        } else {
            // Fallback: SkyrimNet stores save-specific bios in _saves/{saveId}/characters/.
            std::string savesDir = "Data/SKSE/Plugins/SkyrimNet/prompts/_saves";
            if (std::filesystem::exists(savesDir, ec) && std::filesystem::is_directory(savesDir, ec)) {
                std::vector<std::filesystem::directory_entry> saveDirs;
                for (auto& entry : std::filesystem::directory_iterator(savesDir, ec)) {
                    if (entry.is_directory(ec)) {
                        saveDirs.push_back(entry);
                    }
                }
                std::sort(saveDirs.begin(), saveDirs.end(),
                    [](const auto& a, const auto& b) {
                        return a.path().filename().string() > b.path().filename().string();
                    });
                for (auto& saveDir : saveDirs) {
                    auto saveBioPath = saveDir.path() / "characters" / promptFile;
                    if (std::filesystem::exists(saveBioPath, ec)) {
                        filePath = saveBioPath.string();
                        break;
                    }
                }
            }
        }

        std::string summary;
        if (!filePath.empty()) {
            std::ifstream file(filePath);
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
                file.close();

                // Extract {% block summary %}...{% endblock %}
                const std::string startTag = "{% block summary %}";
                const std::string endTag = "{% endblock %}";
                auto startPos = content.find(startTag);
                if (startPos != std::string::npos) {
                    startPos += startTag.size();
                    auto endPos = content.find(endTag, startPos);
                    if (endPos != std::string::npos) {
                        summary = content.substr(startPos, endPos - startPos);
                        while (!summary.empty() && (summary.front() == ' ' || summary.front() == '\n' || summary.front() == '\r'))
                            summary.erase(summary.begin());
                        while (!summary.empty() && (summary.back() == ' ' || summary.back() == '\n' || summary.back() == '\r'))
                            summary.pop_back();
                    }
                }
            }
        }

        if (summary.empty()) {
            logger::warn("MemoryDB: No bio summary for FormID 0x{:08X} (template: '{}', tried: '{}', '{}', and _saves/*/characters/)",
                formId, bioTemplate, dynamicPath, originalPath);
        } else {
            logger::info("MemoryDB: Bio loaded for 0x{:08X} ({}) from {}: {}...",
                formId, bioTemplate, filePath, summary.substr(0, 60));
        }

        {
            std::lock_guard lock(m_mutex);
            m_bioSummaryCache[formId] = summary;
        }

        return summary;
    }

    // =========================================================================
    // GetFormattedMemories
    // =========================================================================

    std::string MemoryDB::GetFormattedMemories(RE::FormID formId, int maxCount) {
        if (!SkyrimNetAPI::GetMemoriesForActor) return "";
        try {
            auto jsonStr = SkyrimNetAPI::GetMemoriesForActor(formId, maxCount, "");
            float currentTime = GetCurrentDBHours();
            return FormatMemoriesFromJson(jsonStr, currentTime);
        } catch (...) {
            logger::warn("MemoryDB: GetFormattedMemories exception for 0x{:08X}", formId);
            return "";
        }
    }

    std::string MemoryDB::FormatMemoriesFromJson(const std::string& jsonStr, float currentTime) {
        try {
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array() || arr.empty()) return "";

            std::string result;
            for (const auto& mem : arr) {
                std::string content = mem.value("content", "");
                if (content.empty()) continue;

                std::string emotion = mem.value("emotion", "");
                std::string memType = mem.value("type", "");
                double gameTime = mem.value("game_time", 0.0);

                std::string sanitized = SanitizeForPrompt(content);
                std::string relTime = FormatRelativeTime(
                    static_cast<float>(gameTime), currentTime);

                if (!result.empty()) result += "\n";
                result += "- [";
                result += memType;
                if (!emotion.empty()) {
                    result += ", ";
                    result += emotion;
                }
                result += "] ";
                result += sanitized;
                result += " (";
                result += relTime;
                result += ")";
            }
            return result;
        } catch (...) {
            return "";
        }
    }

    // =========================================================================
    // GetFormattedRecentEvents
    // =========================================================================

    std::string MemoryDB::GetFormattedRecentEvents(int maxCount, const std::string& eventTypeFilter) {
        if (!SkyrimNetAPI::GetRecentEvents) return "";
        try {
            auto jsonStr = SkyrimNetAPI::GetRecentEvents(
                0, maxCount, eventTypeFilter.empty() ? nullptr : eventTypeFilter.c_str());
            return FormatWorldEventsFromJson(jsonStr);
        } catch (...) {
            logger::warn("MemoryDB: GetFormattedRecentEvents exception");
            return "";
        }
    }

    std::string MemoryDB::FormatWorldEventsFromJson(const std::string& jsonStr) {
        try {
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array() || arr.empty()) return "";

            std::string result;
            for (const auto& evt : arr) {
                std::string eventType = evt.value("type", "");
                double gameTime = evt.value("gameTime", 0.0);
                std::string originName = evt.value("originatingActorName", "");
                std::string targetName = evt.value("targetActorName", "");

                // Extract display text from data
                std::string eventData;
                if (evt.contains("data")) {
                    if (evt["data"].is_string()) {
                        eventData = evt["data"].get<std::string>();
                    } else {
                        eventData = evt["data"].dump();
                    }
                }

                std::string displayText = ExtractEventDisplayText(eventType, eventData);
                displayText = Truncate(SanitizeForPrompt(displayText), 100);
                if (displayText.empty()) continue;

                // Format time as HH:MM AM/PM
                double secondOfDay = std::fmod(gameTime, 86400.0);
                double hourOfDay = secondOfDay / SECONDS_PER_HOUR;
                int hour = static_cast<int>(hourOfDay);
                int minute = static_cast<int>((secondOfDay - hour * 3600.0) / 60.0);
                bool isPM = hour >= 12;
                int displayHour = hour % 12;
                if (displayHour == 0) displayHour = 12;

                if (!result.empty()) result += "\n";
                result += fmt::format("- [{:d}:{:02d} {}] ",
                    displayHour, minute, isPM ? "PM" : "AM");

                if (eventType == "dialogue" || eventType == "dialogue_background" ||
                    eventType == "dialogue_npc" || eventType == "dialogue_player" ||
                    eventType == "dialogue_player_text") {
                    if (!originName.empty() && !targetName.empty()) {
                        result += originName + " told " + targetName + ": \"" + displayText + "\"";
                    } else if (!originName.empty()) {
                        result += originName + ": \"" + displayText + "\"";
                    } else {
                        result += displayText;
                    }
                } else if (eventType == "direct_narration" || eventType == "death") {
                    result += displayText;
                } else {
                    if (!originName.empty()) {
                        result += originName + ": " + displayText;
                    } else {
                        result += displayText;
                    }
                }
            }
            return result;
        } catch (...) {
            return "";
        }
    }

    // =========================================================================
    // GetRecentEventsForActor
    // =========================================================================

    std::string MemoryDB::GetRecentEventsForActor(RE::FormID formId, int maxCount) {
        if (!SkyrimNetAPI::GetRecentEvents) return "";
        try {
            auto jsonStr = SkyrimNetAPI::GetRecentEvents(
                formId, maxCount, "direct_narration,custom_action,dialogue,persistent_generic");
            float currentTime = GetCurrentDBHours();
            return FormatActorEventsFromJson(jsonStr, currentTime);
        } catch (...) {
            logger::warn("MemoryDB: GetRecentEventsForActor exception for 0x{:08X}", formId);
            return "";
        }
    }

    std::string MemoryDB::FormatActorEventsFromJson(const std::string& jsonStr, float currentTime) {
        try {
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array() || arr.empty()) return "";

            std::string result;
            for (const auto& evt : arr) {
                std::string eventType = evt.value("type", "");
                double gameTime = evt.value("gameTime", 0.0);
                std::string originName = evt.value("originatingActorName", "");
                std::string targetName = evt.value("targetActorName", "");

                std::string eventData;
                if (evt.contains("data")) {
                    if (evt["data"].is_string()) {
                        eventData = evt["data"].get<std::string>();
                    } else {
                        eventData = evt["data"].dump();
                    }
                }

                std::string displayText = ExtractEventDisplayText(eventType, eventData);
                displayText = Truncate(SanitizeForPrompt(displayText), 80);
                if (displayText.empty()) continue;

                std::string timeAgo = FormatRelativeTime(
                    static_cast<float>(gameTime), currentTime);

                if (!result.empty()) result += "; ";
                result += "[" + timeAgo + "] ";

                if (eventType == "dialogue") {
                    if (!originName.empty() && !targetName.empty()) {
                        result += originName + " spoke with " + targetName;
                    } else {
                        result += displayText;
                    }
                } else {
                    result += displayText;
                }
            }
            return result;
        } catch (...) {
            return "";
        }
    }

    // =========================================================================
    // GetActiveStoryNPCs
    // =========================================================================

    std::string MemoryDB::GetActiveStoryNPCs(int maxCount) {
        if (!SkyrimNetAPI::GetActorEngagement) return "";
        try {
            // Get raw stats, apply IntelEngine's scoring (24h=3x, 7d=1x, older=0.3x)
            auto jsonStr = SkyrimNetAPI::GetActorEngagement(0, true, false, 86400.0, 604800.0);
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array() || arr.empty()) return "";

            struct Scored {
                std::string name;
                float score;
            };
            std::vector<Scored> scored;

            for (const auto& actor : arr) {
                float memScore =
                    actor.value("recentMemoryImportanceShort", 0.0) * 3.0f +
                    (actor.value("recentMemoryImportanceMedium", 0.0) - actor.value("recentMemoryImportanceShort", 0.0)) * 1.0f +
                    (actor.value("totalMemoryImportance", 0.0) - actor.value("recentMemoryImportanceMedium", 0.0)) * 0.3f;

                float evtScore =
                    actor.value("recentEventCountShort", 0) * 3.0f +
                    (actor.value("recentEventCountMedium", 0) - actor.value("recentEventCountShort", 0)) * 1.0f +
                    (actor.value("eventCount", 0) - actor.value("recentEventCountMedium", 0)) * 0.3f;

                float total = memScore + evtScore;
                if (total > 0.0f) {
                    scored.push_back({actor.value("name", ""), total});
                }
            }

            std::sort(scored.begin(), scored.end(),
                [](const Scored& a, const Scored& b) { return a.score > b.score; });

            std::string result;
            int count = 0;
            for (const auto& s : scored) {
                if (count >= maxCount) break;
                if (s.name.empty()) continue;
                if (!result.empty()) result += ", ";
                result += s.name;
                count++;
            }
            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetActiveStoryNPCs exception");
            return "";
        }
    }

    // =========================================================================
    // GetRelationshipSummary
    // =========================================================================

    std::string MemoryDB::GetRelationshipSummary(RE::FormID formId1, RE::FormID formId2) {
        if (!SkyrimNetAPI::GetRecentEvents || !SkyrimNetAPI::GetMemoriesForActor) return "";
        try {
            // Get recent events involving both actors
            auto eventsJson1 = SkyrimNetAPI::GetRecentEvents(
                formId1, 20, "");
            float currentTime = GetCurrentDBHours();

            auto arr = nlohmann::json::parse(eventsJson1);
            if (!arr.is_array()) return "";

            // Get actor2's name for filtering
            std::string name2;
            if (SkyrimNetAPI::GetVersion) {
                // Use GetRecentEvents for formId2 to discover name
                auto eventsJson2 = SkyrimNetAPI::GetRecentEvents(formId2, 1, "");
                auto arr2 = nlohmann::json::parse(eventsJson2);
                if (arr2.is_array() && !arr2.empty()) {
                    name2 = arr2[0].value("originatingActorName", "");
                }
            }

            std::string result;
            int count = 0;

            // Filter events involving both actors
            for (const auto& evt : arr) {
                std::string origName = evt.value("originatingActorName", "");
                std::string targName = evt.value("targetActorName", "");

                // Check if actor2 is involved in this event
                bool actor2Involved = false;
                if (!name2.empty()) {
                    actor2Involved = (origName == name2 || targName == name2);
                }

                if (!actor2Involved) continue;
                if (count >= 5) break;

                std::string eventData;
                if (evt.contains("data")) {
                    eventData = evt["data"].is_string() ? evt["data"].get<std::string>() : evt["data"].dump();
                }

                std::string text = Truncate(SanitizeForPrompt(
                    ExtractEventDisplayText(evt.value("type", ""), eventData)), 80);
                double gameTime = evt.value("gameTime", 0.0);
                std::string relTime = FormatRelativeTime(
                    static_cast<float>(gameTime), currentTime);

                if (!result.empty()) result += "\n";
                result += "- " + text + " (" + relTime + ")";
                count++;
            }

            // Also check memories of actor1 that mention actor2
            auto memoriesJson = SkyrimNetAPI::GetMemoriesForActor(formId1, 50, "");
            auto memArr = nlohmann::json::parse(memoriesJson);
            if (memArr.is_array() && !name2.empty()) {
                std::string name1;
                // Get actor1's name from events
                for (const auto& evt : arr) {
                    std::string origName = evt.value("originatingActorName", "");
                    if (!origName.empty() && origName != name2) {
                        name1 = origName;
                        break;
                    }
                }

                int memCount = 0;
                for (const auto& mem : memArr) {
                    if (memCount >= 3) break;
                    std::string content = mem.value("content", "");

                    // Check if content mentions actor2's name (case-insensitive)
                    std::string lowerContent = content;
                    std::string lowerName2 = name2;
                    std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(),
                        [](unsigned char c) { return std::tolower(c); });
                    std::transform(lowerName2.begin(), lowerName2.end(), lowerName2.begin(),
                        [](unsigned char c) { return std::tolower(c); });

                    if (lowerContent.find(lowerName2) == std::string::npos) continue;

                    if (!result.empty()) result += "\n";
                    result += "- " + (name1.empty() ? "NPC" : name1) + " remembers: " +
                        Truncate(SanitizeForPrompt(content), 80);
                    memCount++;
                }
            }

            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetRelationshipSummary exception");
            return "";
        }
    }

    // =========================================================================
    // Story Candidate Selection
    // =========================================================================

    std::vector<RankedCandidate> MemoryDB::GetRankedCandidateFormIDs(int maxCount) {
        if (!SkyrimNetAPI::GetActorEngagement) return {};
        try {
            // Request extra candidates so IntelEngine's scoring has a wider pool
            auto jsonStr = SkyrimNetAPI::GetActorEngagement(maxCount * 4, true, true, 86400.0, 604800.0);
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array()) {
                logger::warn("MemoryDB: GetActorEngagement returned non-array: {}",
                    jsonStr.size() > 200 ? jsonStr.substr(0, 200) + "..." : jsonStr);
                return {};
            }

            logger::debug("MemoryDB: GetActorEngagement returned {} entries", arr.size());

            // Apply IntelEngine's recency scoring: 24h=3x, 7d=1x, older=0.3x
            std::vector<RankedCandidate> result;
            int skippedFormId0 = 0, skippedPlayer = 0, skippedEmpty = 0, skippedScore0 = 0;
            for (const auto& actor : arr) {
                uint32_t formId = actor.value("formId", 0u);
                std::string name = actor.value("name", "");
                if (formId == 0) { skippedFormId0++; continue; }
                if (formId == 0x14) { skippedPlayer++; continue; }
                if (name.empty()) { skippedEmpty++; continue; }

                float memScore =
                    static_cast<float>(actor.value("recentMemoryImportanceShort", 0.0)) * 3.0f +
                    static_cast<float>(actor.value("recentMemoryImportanceMedium", 0.0) - actor.value("recentMemoryImportanceShort", 0.0)) * 1.0f +
                    static_cast<float>(actor.value("totalMemoryImportance", 0.0) - actor.value("recentMemoryImportanceMedium", 0.0)) * 0.3f;

                float evtScore =
                    actor.value("recentEventCountShort", 0) * 3.0f +
                    (actor.value("recentEventCountMedium", 0) - actor.value("recentEventCountShort", 0)) * 1.0f +
                    (actor.value("eventCount", 0) - actor.value("recentEventCountMedium", 0)) * 0.3f;

                float total = memScore + evtScore;
                if (total > 0.0f) {
                    result.push_back({static_cast<RE::FormID>(formId), name, total});
                } else {
                    skippedScore0++;
                }
            }

            logger::debug("MemoryDB: GetRankedCandidateFormIDs filter: formId0={}, player={}, empty={}, score0={}, passed={}",
                skippedFormId0, skippedPlayer, skippedEmpty, skippedScore0, result.size());

            std::sort(result.begin(), result.end(),
                [](const RankedCandidate& a, const RankedCandidate& b) { return a.score > b.score; });

            if (static_cast<int>(result.size()) > maxCount) {
                result.resize(maxCount);
            }

            logger::debug("MemoryDB: GetRankedCandidateFormIDs returned {} candidates", result.size());
            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetRankedCandidateFormIDs exception");
            return {};
        }
    }

    std::vector<RankedCandidate> MemoryDB::GetRelatedCandidateFormIDs(RE::FormID actorFormId, int maxCount) {
        if (!SkyrimNetAPI::GetRelatedActors) return {};
        try {
            auto jsonStr = SkyrimNetAPI::GetRelatedActors(actorFormId, maxCount * 2, 86400.0, 604800.0);
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array()) return {};

            float currentTime = GetCurrentDBHours();
            std::vector<RankedCandidate> result;

            for (const auto& actor : arr) {
                uint32_t formId = actor.value("formId", 0u);
                std::string name = actor.value("name", "");
                if (formId == 0 || formId == 0x14 || name.empty()) continue;

                // Apply recency weighting to shared events
                float score =
                    actor.value("recentSharedEventsShort", 0) * 3.0f +
                    (actor.value("recentSharedEventsMedium", 0) - actor.value("recentSharedEventsShort", 0)) * 1.0f +
                    (actor.value("sharedEventCount", 0) - actor.value("recentSharedEventsMedium", 0)) * 0.3f;

                if (score > 0.0f) {
                    result.push_back({static_cast<RE::FormID>(formId), name, score});
                }
            }

            std::sort(result.begin(), result.end(),
                [](const RankedCandidate& a, const RankedCandidate& b) { return a.score > b.score; });

            if (static_cast<int>(result.size()) > maxCount) {
                result.resize(maxCount);
            }

            logger::debug("MemoryDB: GetRelatedCandidateFormIDs for 0x{:08X} returned {} candidates",
                actorFormId, result.size());
            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetRelatedCandidateFormIDs exception");
            return {};
        }
    }

    std::unordered_set<std::string> MemoryDB::GetRecentPlayerInteractionNames(float withinHours) {
        if (!SkyrimNetAPI::GetPlayerContext) return {};
        try {
            auto jsonStr = SkyrimNetAPI::GetPlayerContext(withinHours);
            auto obj = nlohmann::json::parse(jsonStr);

            std::unordered_set<std::string> result;
            if (obj.contains("recentInteractionNames") && obj["recentInteractionNames"].is_array()) {
                for (const auto& name : obj["recentInteractionNames"]) {
                    if (name.is_string() && !name.get<std::string>().empty()) {
                        result.insert(name.get<std::string>());
                    }
                }
            }

            // Update cached current time while we have it
            if (obj.contains("currentTime")) {
                std::lock_guard lock(m_mutex);
                m_cachedCurrentTime = static_cast<float>(obj.value("currentTime", 0.0));
            }

            logger::debug("MemoryDB: {} NPCs interacted with player in last {:.0f}h",
                result.size(), withinHours);
            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetRecentPlayerInteractionNames exception");
            return {};
        }
    }

    std::vector<PlayerRelationship> MemoryDB::GetPlayerRelationshipData() {
        if (!SkyrimNetAPI::GetPlayerContext) return {};
        try {
            auto jsonStr = SkyrimNetAPI::GetPlayerContext(0.0f);  // 0 = all time
            auto obj = nlohmann::json::parse(jsonStr);

            // Update cached current time
            if (obj.contains("currentTime")) {
                std::lock_guard lock(m_mutex);
                m_cachedCurrentTime = static_cast<float>(obj.value("currentTime", 0.0));
            }

            std::vector<PlayerRelationship> result;
            if (obj.contains("relationships") && obj["relationships"].is_array()) {
                for (const auto& rel : obj["relationships"]) {
                    uint32_t formId = rel.value("formId", 0u);
                    std::string name = rel.value("name", "");
                    int count = rel.value("interactionCount", 0);
                    float lastTime = static_cast<float>(rel.value("lastInteractionTime", 0.0));

                    if (formId > 0 && formId != 0x14 && !name.empty()) {
                        result.push_back({
                            static_cast<RE::FormID>(formId),
                            name,
                            count,
                            lastTime
                        });
                    }
                }
            }

            logger::debug("MemoryDB: GetPlayerRelationshipData returned {} NPCs", result.size());
            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetPlayerRelationshipData exception");
            return {};
        }
    }

    std::vector<RankedCandidate> MemoryDB::GetSociallyActiveFormIDs(int maxCount) {
        if (!SkyrimNetAPI::GetActorEngagement) return {};
        try {
            // Get all actor stats without player events filter (we want NPC-to-NPC)
            auto jsonStr = SkyrimNetAPI::GetActorEngagement(0, true, false, 86400.0, 604800.0);
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array()) return {};

            std::vector<RankedCandidate> result;
            for (const auto& actor : arr) {
                uint32_t formId = actor.value("formId", 0u);
                std::string name = actor.value("name", "");
                int npcToNpc = actor.value("npcToNpcEventCount", 0);
                if (formId == 0 || name.empty() || npcToNpc == 0) continue;

                result.push_back({
                    static_cast<RE::FormID>(formId),
                    name,
                    static_cast<float>(npcToNpc)
                });
            }

            std::sort(result.begin(), result.end(),
                [](const RankedCandidate& a, const RankedCandidate& b) { return a.score > b.score; });

            if (static_cast<int>(result.size()) > maxCount) {
                result.resize(maxCount);
            }

            logger::debug("MemoryDB: GetSociallyActiveFormIDs returned {} NPCs", result.size());
            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetSociallyActiveFormIDs exception");
            return {};
        }
    }

    std::vector<NPCPairRelationship> MemoryDB::GetPoolRelationships(
        const std::vector<RE::FormID>& poolFormIds) {
        if (!SkyrimNetAPI::GetEventPairCounts) return {};
        if (poolFormIds.size() < 2) return {};
        try {
            // Build comma-separated FormID list
            std::string csvList;
            for (auto fid : poolFormIds) {
                if (fid == 0x14) continue;
                if (!csvList.empty()) csvList += ",";
                csvList += std::to_string(fid);
            }
            if (csvList.empty()) return {};

            auto jsonStr = SkyrimNetAPI::GetEventPairCounts(csvList.c_str(), 3);
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array()) return {};

            std::vector<NPCPairRelationship> result;
            for (const auto& pair : arr) {
                uint32_t fid1 = pair.value("formId1", 0u);
                uint32_t fid2 = pair.value("formId2", 0u);
                int shared = pair.value("sharedEvents", 0);
                if (fid1 > 0 && fid2 > 0) {
                    result.push_back({
                        static_cast<RE::FormID>(fid1),
                        static_cast<RE::FormID>(fid2),
                        shared
                    });
                }
            }

            // Already sorted by SkyrimNet API
            logger::debug("MemoryDB: GetPoolRelationships found {} pairs", result.size());
            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetPoolRelationships exception");
            return {};
        }
    }

    // =========================================================================
    // Dialogue Safety Net Functions
    // =========================================================================

    std::string MemoryDB::GetRecentDialogueForActor(RE::FormID formId, int maxExchanges) {
        if (!SkyrimNetAPI::GetRecentDialogue) return "";
        try {
            auto jsonStr = SkyrimNetAPI::GetRecentDialogue(formId, maxExchanges);
            auto arr = nlohmann::json::parse(jsonStr);
            if (!arr.is_array() || arr.empty()) return "";

            // Resolve player name once
            std::string playerName = "Player";
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                auto* name = player->GetDisplayFullName();
                if (name && name[0]) playerName = name;
            }

            // Build conversation text from JSON array
            std::string result;
            for (const auto& entry : arr) {
                std::string speaker = entry.value("speaker", "");
                std::string npcName = entry.value("npcName", "NPC");
                std::string data = entry.value("data", "");

                if (data.empty()) continue;

                std::string speakerName;
                if (speaker == "player") {
                    speakerName = playerName;
                } else {
                    speakerName = npcName;
                }

                std::string sanitized = SanitizeForPrompt(data);
                if (!result.empty()) result += "\n";
                result += speakerName + ": " + sanitized;
            }

            return result;
        } catch (...) {
            logger::warn("MemoryDB: GetRecentDialogueForActor exception for 0x{:08X}", formId);
            return "";
        }
    }

    LatestDialogueInfo MemoryDB::GetLatestDialogueInfo() {
        if (!SkyrimNetAPI::GetLatestDialogueInfo) return {};
        try {
            auto jsonStr = SkyrimNetAPI::GetLatestDialogueInfo();
            auto obj = nlohmann::json::parse(jsonStr);

            uint32_t npcFormId = obj.value("npcFormId", 0u);
            float gameTime = static_cast<float>(obj.value("gameTime", 0.0));

            if (npcFormId == 0) return {};

            return {static_cast<RE::FormID>(npcFormId), gameTime};
        } catch (...) {
            logger::warn("MemoryDB: GetLatestDialogueInfo exception");
            return {};
        }
    }

    // =========================================================================
    // GetCurrentDBHours
    // =========================================================================

    float MemoryDB::GetCurrentDBHours() {
        // Use live game time from Calendar — this is the actual current moment.
        // The previous approach used MAX(game_time) FROM events via GetPlayerContext,
        // which returns the timestamp of the most recent event, NOT the current time.
        // When events cluster together (e.g., rapid dialogue), MAX(game_time) ≈ all
        // recent event times, making FormatRelativeTime return "just now" for everything.
        auto* cal = RE::Calendar::GetSingleton();
        if (cal) {
            return cal->GetCurrentGameTime() * 86400.0f;  // days → seconds
        }
        // Calendar unavailable (very early init) — fall back to DB max time
        if (SkyrimNetAPI::GetPlayerContext) {
            try {
                auto jsonStr = SkyrimNetAPI::GetPlayerContext(0.0f);
                auto obj = nlohmann::json::parse(jsonStr);
                return static_cast<float>(obj.value("currentTime", 0.0));
            } catch (...) {}
        }
        std::lock_guard lock(m_mutex);
        return m_cachedCurrentTime > 0.0f ? m_cachedCurrentTime : 0.0f;
    }

    // =========================================================================
    // Helper Functions (static utilities — unchanged)
    // =========================================================================

    std::string MemoryDB::FormatRelativeTime(float gameTimeSeconds, float currentSecondsParam) {
        float hoursAgo = (currentSecondsParam - gameTimeSeconds) / SECONDS_PER_HOUR;

        if (hoursAgo < 0.1f)   return "just now";
        if (hoursAgo < 0.25f)  return "minutes ago";
        if (hoursAgo < 0.5f)   return "half an hour ago";
        if (hoursAgo < 1.0f)   return "less than an hour ago";
        if (hoursAgo < 3.0f)   return "a few hours ago";
        if (hoursAgo < 12.0f)  return "earlier today";
        if (hoursAgo < 24.0f)  return "yesterday";
        if (hoursAgo < 48.0f)  return "a day ago";
        if (hoursAgo < 72.0f)  return "a couple of days ago";
        if (hoursAgo < 168.0f) return "several days ago";

        int days = static_cast<int>(hoursAgo / 24.0f);
        return std::to_string(days) + " days ago";
    }

    std::string MemoryDB::ExtractEventDisplayText(const std::string& eventType, const std::string& eventDataStr) {
        if (eventDataStr.empty()) return "";

        // Helper to extract a JSON string field value
        auto extractField = [&](const char* field) -> std::string {
            std::string needle = std::string("\"") + field + "\":\"";
            auto pos = eventDataStr.find(needle);
            if (pos == std::string::npos) return "";
            auto start = pos + needle.size();
            auto end = start;
            while (end < eventDataStr.size()) {
                if (eventDataStr[end] == '"' && (end == 0 || eventDataStr[end - 1] != '\\')) break;
                ++end;
            }
            return eventDataStr.substr(start, end - start);
        };

        if (eventType == "dialogue" || eventType == "dialogue_background" ||
            eventType == "dialogue_npc" || eventType == "dialogue_player" ||
            eventType == "dialogue_player_text") {
            return extractField("dialogue");
        }
        if (eventType == "direct_narration") {
            return extractField("narration");
        }
        if (eventType == "persistent_generic") {
            return extractField("line");
        }
        if (eventType == "death") {
            std::string victim = extractField("victim");
            std::string killer = extractField("killer");
            if (!victim.empty() && !killer.empty()) {
                return victim + " was killed by " + killer;
            }
            if (!victim.empty()) {
                return victim + " died";
            }
            return "someone died";
        }

        // custom_action and other types: plain text (no JSON wrapper)
        if (eventDataStr.empty() || eventDataStr[0] != '{') {
            return eventDataStr;
        }

        // Unknown JSON format — try common field names
        std::string text = extractField("text");
        if (text.empty()) text = extractField("content");
        if (text.empty()) text = extractField("msg");
        if (text.empty()) text = extractField("narration");
        return text.empty() ? eventDataStr : text;
    }

    std::string MemoryDB::Truncate(const std::string& text, size_t maxLen) {
        if (text.size() <= maxLen) return text;
        return text.substr(0, maxLen - 3) + "...";
    }

    std::string MemoryDB::SanitizeForPrompt(const std::string& text) {
        std::string result;
        result.reserve(text.size());
        for (char c : text) {
            if (c == '\n' || c == '\r') {
                result += ' ';
            } else {
                result += c;
            }
        }
        return result;
    }

    std::string MemoryDB::EscapeJsonString(const std::string& text) {
        std::string result;
        result.reserve(text.size() + 16);
        for (char c : text) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n";  break;
                case '\r': break;
                case '\t': result += "\\t";  break;
                default:   result += c;      break;
            }
        }
        return result;
    }

    int MemoryDB::CheckScheduleKeywords(const std::string& text) {
        std::string lower;
        lower.reserve(text.size());
        for (char c : text) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        auto contains = [&](const char* phrase) {
            return lower.find(phrase) != std::string::npos;
        };

        // Fetch/bring keywords (check first — more specific)
        if (contains("bring him") || contains("bring her") || contains("bring them") ||
            contains("fetch") || contains("go get") || contains("find and bring")) {
            return 2;
        }

        // Delivery keywords
        if (contains("tell him") || contains("tell her") || contains("tell them") ||
            contains("let him know") || contains("let her know") ||
            contains("send word") || contains("deliver a message") ||
            contains("message to") || contains("inform")) {
            return 3;
        }

        // Meeting keywords
        bool hasStrongCommitment =
            contains("see you tonight") || contains("see you tomorrow") ||
            contains("see you at") || contains("i'll be there") ||
            contains("until then") || contains("don't be late") ||
            contains("don't keep me waiting") || contains("expect you");

        if (hasStrongCommitment) return 1;

        bool hasTimeWord =
            contains("tonight") || contains("tomorrow") || contains("morning") ||
            contains("evening") || contains("afternoon") || contains("dawn") ||
            contains("dusk") || contains("sunset") || contains("sunrise") ||
            contains("noon") || contains("midnight") || contains("later") ||
            contains("soon") || contains("hour") || contains("nightfall") ||
            contains("first light") || contains("after dark") || contains("before dark");

        bool hasMeetingWord =
            contains("meet") || contains("see you") || contains("be there") ||
            contains("wait for") || contains("join") || contains("come by") ||
            contains("come to") || contains("i'll come") ||
            contains("find me") || contains("look for me") ||
            contains("rendezvous") || contains("gather");

        if (hasTimeWord && hasMeetingWord) return 1;

        return 0;
    }

}  // namespace IntelEngine
