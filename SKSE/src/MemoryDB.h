#pragma once

/**
 * MemoryDB - SkyrimNet SQLite Database Reader
 *
 * Reads NPC memories, events, and conversation history directly from
 * SkyrimNet's SQLite database. Provides formatted text for LLM prompt
 * injection via BuildActorContextJson and standalone Papyrus natives.
 *
 * Design:
 * - Read-only access (SQLITE_OPEN_READONLY) - safe alongside SkyrimNet writes
 * - Lazy initialization (DB discovered on first query, not at kDataLoaded)
 * - UUID mappings cached (small table, ~854 rows)
 * - Memories/events NOT cached (grow during session)
 * - Graceful degradation (returns empty strings if DB unavailable)
 */

#include "Plugin.h"

#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>

struct sqlite3;       // Forward declaration
struct sqlite3_stmt;  // Forward declaration

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
         * Find and open the latest SkyrimNet database.
         * Thread-safe: acquires exclusive lock.
         */
        void Connect();

        /**
         * Close the database connection.
         * Called on kNewGame to force re-discovery.
         */
        void Disconnect();

        /**
         * Check if database is connected and queryable.
         */
        bool IsConnected() const;

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
         *
         * BUG WORKAROUND: Uses name-based matching instead of FormID because:
         * 1) The player ("Galanx") can have MULTIPLE UUIDs in the DB (e.g., 3734...
         *    with 126 events, 4819... with 2407 events). FormIdToUUID(0x14) only
         *    returns one, missing ~95% of player interaction events.
         * 2) NPC FormIDs stored in uuid_mappings can become stale across mod updates
         *    (e.g., Heidi DB=0xBC027633 vs Game=0xBC018519). Name matching survives
         *    load-order and FormID changes.
         */
        std::unordered_set<std::string> GetRecentPlayerInteractionNames(float withinHours);

        /**
         * Get relationship data between the player and all NPCs they've ever interacted with.
         * Returns interaction count + last interaction time per NPC.
         * Used for absence-bonus scoring in story candidate ranking.
         */
        std::vector<PlayerRelationship> GetPlayerRelationshipData();

        /**
         * Get NPCs ranked by NPC-to-NPC event density (excluding player interactions).
         * Returns "social butterflies" — NPCs with rich NPC-NPC interaction history.
         * Used to include non-player-centric candidates in the story pool.
         */
        std::vector<RankedCandidate> GetSociallyActiveFormIDs(int maxCount);

        /**
         * Get NPC-NPC relationship pairs within a set of pool FormIDs.
         * Returns pairs with shared event counts (both directions combined).
         * Used to annotate the DM markdown with intra-pool connections.
         */
        std::vector<NPCPairRelationship> GetPoolRelationships(const std::vector<RE::FormID>& poolFormIds);

        // =================================================================
        // Dialogue Safety Net Functions
        // =================================================================

        /**
         * Get recent dialogue between the player and a specific NPC.
         * Returns raw formatted conversation text (caller handles escaping).
         * Queries dialogue + dialogue_player_text events from SQLite.
         *
         * @param formId NPC's Skyrim FormID
         * @param maxExchanges Maximum conversation exchanges (1 exchange = player + NPC line)
         * @return JSON-escaped conversation text, or empty string
         */
        std::string GetRecentDialogueForActor(RE::FormID formId, int maxExchanges);

        /**
         * Find the most recent NPC the player had a dialogue with, plus event timestamp.
         * Queries the events table for the latest 'dialogue' event targeting the player.
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
         * SkyrimNet stores game_time as GameDaysPassed * 86400 (game-seconds).
         * Uses MAX(game_time) from events — consistent with all DB timestamps.
         * Thread-safe: acquires own lock. Used by NPCIndex for scoring.
         */
        float GetCurrentDBHours();

    private:
        MemoryDB() = default;
        ~MemoryDB();
        MemoryDB(const MemoryDB&) = delete;
        MemoryDB& operator=(const MemoryDB&) = delete;

        // Ensure connection exists (lazy init)
        void EnsureConnected();

        // Find latest SkyrimNet-*.db file
        std::string FindLatestDatabase();

        // Build UUID cache from uuid_mappings table (caller must hold exclusive lock)
        void RefreshUUIDCacheInternal();

        // Resolve Skyrim FormID to SkyrimNet UUID. Returns 0 if not found.
        int64_t FormIdToUUID(RE::FormID formId) const;

        // Resolve SkyrimNet UUID to actor display name. Returns empty if not found.
        std::string UUIDToName(int64_t uuid) const;

        // Get current time in DB seconds from DB's own timeline.
        // Must be called while holding m_mutex. Falls back to Calendar if DB empty.
        float GetDBCurrentHoursLocked();

        // Format relative time from game_time (in DB seconds) given current seconds
        static std::string FormatRelativeTime(float gameTimeSeconds, float currentSeconds);

        // Extract display text from event_data JSON by event type
        static std::string ExtractEventDisplayText(const std::string& eventType, const char* eventData);

        // Truncate text to maxLen characters with "..."
        static std::string Truncate(const std::string& text, size_t maxLen);

        // Sanitize text for safe prompt embedding (newlines → spaces, quotes → single)
        static std::string SanitizeForPrompt(const std::string& text);

        // Get or create a prepared statement from cache. Returns nullptr on failure.
        // Caller must NOT finalize the returned stmt — cache owns its lifecycle.
        // Caller must bind params after this call; stmt is already reset.
        sqlite3_stmt* PrepareOrGet(const char* sql);

        // Finalize all cached prepared statements (call before closing DB)
        void ClearStatementCache();

        // Thread safety
        mutable std::shared_mutex m_mutex;

        // SQLite database handle
        sqlite3* m_db = nullptr;

        // Prepared statement cache (key = SQL string, value = compiled stmt)
        std::unordered_map<std::string, sqlite3_stmt*> m_stmtCache;

        // Cached UUID mappings
        std::unordered_map<RE::FormID, int64_t> m_formIdToUUID;
        std::unordered_map<int64_t, RE::FormID> m_uuidToFormId;
        std::unordered_map<int64_t, std::string> m_uuidToName;

        // Connection state
        std::string m_dbPath;
        bool m_connectionAttempted = false;
    };

}  // namespace IntelEngine
