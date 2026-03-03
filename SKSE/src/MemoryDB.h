#pragma once

/**
 * MemoryDB - SkyrimNet Data API Client
 *
 * Reads NPC memories, events, and conversation history via SkyrimNet's
 * PublicAPI (dllexport functions). Provides formatted text for LLM prompt
 * injection via BuildActorContextJson and standalone Papyrus natives.
 *
 * Design:
 * - All data flows through SkyrimNet's PublicAPI (no direct SQLite access)
 * - SkyrimNet owns the DB connection, WAL checkpoints, and UUID resolution
 * - IntelEngine owns scoring logic, formatting, and prompt assembly
 * - Graceful degradation (returns empty strings if API unavailable)
 */

#include "Plugin.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

namespace IntelEngine {

    struct RankedCandidate {
        RE::FormID formId;
        std::string name;   // from uuid_mappings.actor_name (reliable across load-order changes)
        float score;
    };

    struct PlayerRelationship {
        RE::FormID formId;
        std::string name;   // from uuid_mappings.actor_name
        int interactionCount;        // Total events involving both player and this NPC
        float lastInteractionHours;  // game_time (in DB seconds) of most recent interaction
    };

    struct LatestDialogueInfo {
        RE::FormID npcFormId = 0;
        float gameTimeHours = 0.0f;  // game_time (in DB seconds) of latest dialogue event
    };

    struct NPCPairRelationship {
        RE::FormID formId1;
        RE::FormID formId2;
        int sharedEvents;            // Total directed events between this pair
    };

    class MemoryDB {
    public:
        static MemoryDB* GetSingleton() {
            static MemoryDB instance;
            return &instance;
        }

        /**
         * Initialize the SkyrimNet API function pointers.
         * Must be called during kDataLoaded before any queries.
         */
        void InitializeAPI();

        /**
         * Check if SkyrimNet's memory system is ready for queries.
         */
        bool IsConnected() const;

        /**
         * Clear cached data (bio summaries, DB time).
         * Called on kNewGame/kPostLoadGame to reset session state.
         */
        void ClearCaches();

        // =================================================================
        // Formatted Query Functions (return LLM-ready text)
        // =================================================================

        /**
         * Get formatted memories for an NPC by Skyrim FormID.
         * Ordered by game_time DESC (most recent first).
         * Queries ALL UUIDs for this actor (handles multi-session UUID splits).
         *
         * @param formId Skyrim FormID (e.g., actor->GetFormID())
         * @param maxCount Maximum memories to return
         * @return Formatted memory text, or empty string if unavailable
         */
        std::string GetFormattedMemories(RE::FormID formId, int maxCount);

        /**
         * Get recent world events formatted for LLM context.
         *
         * @param maxCount Maximum events to return
         * @param eventTypeFilter Comma-separated types (e.g., "dialogue,direct_narration"), empty = all
         * @return Formatted event text, or empty string
         */
        std::string GetFormattedRecentEvents(int maxCount, const std::string& eventTypeFilter);

        /**
         * Get NPCs ranked by story activity (memories + events weighted by recency).
         * Excludes player (FormID 0x14).
         *
         * @param maxCount Maximum NPCs to return
         * @return Comma-separated NPC names
         */
        std::string GetActiveStoryNPCs(int maxCount);

        /**
         * Get relationship summary between two NPCs based on shared events/memories.
         *
         * @param formId1 First actor's FormID
         * @param formId2 Second actor's FormID
         * @return Formatted relationship summary
         */
        std::string GetRelationshipSummary(RE::FormID formId1, RE::FormID formId2);

        /**
         * Get recent events involving a specific NPC (as originator or target).
         * Returns concise formatted text for DM candidate context.
         * Filters to story-relevant types: direct_narration, custom_action, dialogue.
         *
         * @param formId NPC's Skyrim FormID
         * @param maxCount Maximum events to return
         * @return Formatted event lines, or empty string
         */
        std::string GetRecentEventsForActor(RE::FormID formId, int maxCount);

        // =================================================================
        // Story Candidate Selection (return FormID+score for C++ filtering)
        // =================================================================

        /**
         * Get NPCs ranked by story engagement (memories + events, recency-weighted).
         * Returns FormID+score pairs for C++ runtime filtering.
         */
        std::vector<RankedCandidate> GetRankedCandidateFormIDs(int maxCount);

        /**
         * Get NPCs ranked by shared event history with a specific actor.
         */
        std::vector<RankedCandidate> GetRelatedCandidateFormIDs(RE::FormID actorFormId, int maxCount);

        /**
         * Get NAMES of NPCs who had events with the player within the last N hours.
         * Returns lowercase actor names for case-insensitive exclusion checks.
         */
        std::unordered_set<std::string> GetRecentPlayerInteractionNames(float withinHours);

        /**
         * Get relationship data between the player and all NPCs they've ever interacted with.
         * Returns interaction count + last interaction time per NPC.
         */
        std::vector<PlayerRelationship> GetPlayerRelationshipData();

        /**
         * Get NPCs ranked by NPC-to-NPC event density (excluding player interactions).
         * Returns "social butterflies" — NPCs with rich NPC-NPC interaction history.
         */
        std::vector<RankedCandidate> GetSociallyActiveFormIDs(int maxCount);

        /**
         * Get NPC-NPC relationship pairs within a set of pool FormIDs.
         * Returns pairs with shared event counts (both directions combined).
         */
        std::vector<NPCPairRelationship> GetPoolRelationships(const std::vector<RE::FormID>& poolFormIds);

        // =================================================================
        // Dialogue Safety Net Functions
        // =================================================================

        /**
         * Get recent dialogue between the player and a specific NPC.
         * Returns raw formatted conversation text (caller handles escaping).
         *
         * @param formId NPC's Skyrim FormID
         * @param maxExchanges Maximum conversation exchanges (1 exchange = player + NPC line)
         * @return JSON-escaped conversation text, or empty string
         */
        std::string GetRecentDialogueForActor(RE::FormID formId, int maxExchanges);

        /**
         * Find the most recent NPC the player had a dialogue with, plus event timestamp.
         *
         * @return LatestDialogueInfo with FormID + game_time (both 0 if not found)
         */
        LatestDialogueInfo GetLatestDialogueInfo();

        /**
         * Check dialogue text for schedule-related keywords.
         * Returns: 0=none, 1=meeting, 2=fetch, 3=delivery
         */
        static int CheckScheduleKeywords(const std::string& text);

        // Escape string for JSON embedding (quotes, backslashes, newlines)
        static std::string EscapeJsonString(const std::string& text);

        /**
         * Get current time in DB seconds from the DB's own timeline.
         * Uses cached value from the most recent PublicGetPlayerContext call.
         */
        float GetCurrentDBHours();

        // Get the bio summary ({% block summary %}) for an NPC from SkyrimNet's prompt files.
        // Returns cached summary or reads from disk on first call. Empty if no bio file exists.
        std::string GetNPCBioSummary(RE::FormID formId);

        // Get the bio relationships ({% block relationships %}) for an NPC.
        // Populated as a side effect of GetNPCBioSummary (same file read).
        std::string GetNPCBioRelationships(RE::FormID formId);

    private:
        MemoryDB() = default;
        ~MemoryDB() = default;
        MemoryDB(const MemoryDB&) = delete;
        MemoryDB& operator=(const MemoryDB&) = delete;

        // Format relative time from game_time (in DB seconds) given current seconds
        static std::string FormatRelativeTime(float gameTimeSeconds, float currentSeconds);

        // Extract display text from event_data JSON by event type
        static std::string ExtractEventDisplayText(const std::string& eventType, const std::string& eventData);

        // Truncate text to maxLen characters with "..."
        static std::string Truncate(const std::string& text, size_t maxLen);

        // Sanitize text for safe prompt embedding (newlines → spaces, quotes → single)
        static std::string SanitizeForPrompt(const std::string& text);

        // Format memories JSON array into LLM-ready text
        std::string FormatMemoriesFromJson(const std::string& json, float currentTime);

        // Format events JSON array into LLM-ready text (world events view)
        std::string FormatWorldEventsFromJson(const std::string& json);

        // Format events JSON array into concise actor-centric text
        std::string FormatActorEventsFromJson(const std::string& json, float currentTime);

        // Thread safety
        mutable std::mutex m_mutex;

        // Bio summary cache: FormID -> extracted {% block summary %} content
        std::unordered_map<RE::FormID, std::string> m_bioSummaryCache;

        // Bio relationships cache: FormID -> extracted {% block relationships %} content
        std::unordered_map<RE::FormID, std::string> m_bioRelationshipsCache;

        // Cached current DB time (refreshed on GetCurrentDBHours)
        float m_cachedCurrentTime = 0.0f;
    };

}  // namespace IntelEngine
