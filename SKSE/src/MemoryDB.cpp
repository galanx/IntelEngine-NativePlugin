/**
 * MemoryDB - SkyrimNet SQLite Database Reader
 *
 * Reads NPC memories, events, and conversation history directly from
 * SkyrimNet's SQLite database files.
 */

#include "MemoryDB.h"
#include "Settings.h"
#include "StringUtils.h"

#include <sqlite3.h>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace IntelEngine {

    // SkyrimNet stores game_time as GameDaysPassed * 86400 (game-seconds since epoch).
    // All our thresholds are written in hours — multiply by this to match DB units.
    constexpr float SECONDS_PER_HOUR = 3600.0f;

    // RAII guard: resets all cached statements on scope exit.
    // Placed after unique_lock in each public method so it destructs BEFORE
    // the lock (LIFO), releasing SQLite read snapshots while still thread-safe.
    // Prevents our prepared statements from holding SHARED locks that block
    // SkyrimNet's write commits.
    struct StmtGuard {
        std::unordered_map<std::string, sqlite3_stmt*>& cache;
        ~StmtGuard() {
            for (auto& [k, s] : cache) sqlite3_reset(s);
        }
    };

    // =========================================================================
    // Lifecycle
    // =========================================================================

    MemoryDB::~MemoryDB() {
        ClearStatementCache();
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }

    sqlite3_stmt* MemoryDB::PrepareOrGet(const char* sql) {
        // Check cache first
        auto it = m_stmtCache.find(sql);
        if (it != m_stmtCache.end()) {
            sqlite3_reset(it->second);
            sqlite3_clear_bindings(it->second);
            return it->second;
        }

        // Prepare and cache
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            logger::debug("MemoryDB: Failed to prepare: {}", sqlite3_errmsg(m_db));
            return nullptr;
        }

        m_stmtCache[sql] = stmt;
        return stmt;
    }

    void MemoryDB::ClearStatementCache() {
        for (auto& [key, stmt] : m_stmtCache) {
            if (stmt) sqlite3_finalize(stmt);
        }
        m_stmtCache.clear();
    }

    void MemoryDB::Connect() {
        std::unique_lock lock(m_mutex);

        // Close existing connection
        ClearStatementCache();
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        m_formIdToUUID.clear();
        m_uuidToFormId.clear();
        m_uuidToName.clear();
        m_dbPath.clear();
        m_connectionAttempted = true;

        std::string dbPath = FindLatestDatabase();
        if (dbPath.empty()) {
            logger::info("MemoryDB: No SkyrimNet database found");
            return;
        }

        int rc = sqlite3_open_v2(dbPath.c_str(), &m_db,
            SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);

        if (rc != SQLITE_OK) {
            logger::warn("MemoryDB: Failed to open {}: {}",
                dbPath, m_db ? sqlite3_errmsg(m_db) : "unknown error");
            if (m_db) {
                sqlite3_close(m_db);
                m_db = nullptr;
            }
            return;
        }

        m_dbPath = dbPath;
        logger::info("MemoryDB: Connected to {}", dbPath);

        // Build UUID cache
        RefreshUUIDCacheInternal();
    }

    void MemoryDB::Disconnect() {
        std::unique_lock lock(m_mutex);

        ClearStatementCache();
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        m_formIdToUUID.clear();
        m_uuidToFormId.clear();
        m_uuidToName.clear();
        m_dbPath.clear();
        m_connectionAttempted = false;

        logger::info("MemoryDB: Disconnected");
    }

    bool MemoryDB::IsConnected() const {
        std::shared_lock lock(m_mutex);
        return m_db != nullptr;
    }

    void MemoryDB::EnsureConnected() {
        {
            std::shared_lock lock(m_mutex);
            if (m_connectionAttempted) return;
        }
        Connect();
    }

    // =========================================================================
    // Database Discovery
    // =========================================================================

    std::string MemoryDB::FindLatestDatabase() {
        // Search paths for SkyrimNet data folder
        std::vector<std::filesystem::path> searchPaths;

        // Priority 1: Settings override path (points directly to a .db file)
        auto* settings = Settings::GetSingleton();
        if (settings && !settings->skyrimNetDbPath.empty()) {
            std::filesystem::path overridePath(settings->skyrimNetDbPath);
            std::error_code ec;
            if (std::filesystem::exists(overridePath, ec)) {
                logger::info("MemoryDB: Using override path {}", overridePath.string());
                return overridePath.string();
            }
        }

        // Priority 2: Relative to game Data folder (works through MO2 USVFS)
        searchPaths.emplace_back("Data/SKSE/Plugins/SkyrimNet/data");

        // Priority 3: Try via SKSE log directory
        auto logDir = logger::log_directory();
        if (logDir) {
            auto dataVia = logDir->parent_path().parent_path() / "SKSE" / "Plugins" / "SkyrimNet" / "data";
            searchPaths.push_back(dataVia);
        }

        // Strategy 1: Find the DB with a WAL sidecar file.
        // SkyrimNet keeps the active save's DB open in WAL mode, creating a -wal file.
        // This is the most reliable indicator of which DB belongs to the current save.
        for (const auto& searchPath : searchPaths) {
            std::error_code ec;
            if (!std::filesystem::exists(searchPath, ec)) continue;

            for (const auto& entry : std::filesystem::directory_iterator(searchPath, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;

                auto filename = entry.path().filename().string();
                if (filename.starts_with("SkyrimNet-") && filename.ends_with(".db-wal")) {
                    // Found WAL file — derive the actual DB path
                    auto dbPath = entry.path().parent_path() /
                        filename.substr(0, filename.size() - 4);  // strip "-wal"
                    if (std::filesystem::exists(dbPath, ec)) {
                        logger::info("MemoryDB: Found active database via WAL: {}", dbPath.string());
                        return dbPath.string();
                    }
                }
            }
        }

        // Strategy 2: Fallback to latest modification time.
        // WAL files may not exist yet if SkyrimNet hasn't opened its DB.
        std::filesystem::path latestDb;
        std::filesystem::file_time_type latestTime{};

        for (const auto& searchPath : searchPaths) {
            std::error_code ec;
            if (!std::filesystem::exists(searchPath, ec)) continue;

            for (const auto& entry : std::filesystem::directory_iterator(searchPath, ec)) {
                if (ec) break;
                if (!entry.is_regular_file()) continue;

                auto filename = entry.path().filename().string();
                if (filename.starts_with("SkyrimNet-") && filename.ends_with(".db")) {
                    auto writeTime = entry.last_write_time(ec);
                    if (!ec && (latestDb.empty() || writeTime > latestTime)) {
                        latestDb = entry.path();
                        latestTime = writeTime;
                    }
                }
            }

            if (!latestDb.empty()) break;
        }

        if (!latestDb.empty()) {
            logger::info("MemoryDB: Found database via mtime fallback: {}", latestDb.string());
        }
        return latestDb.string();
    }

    // =========================================================================
    // UUID Cache
    // =========================================================================

    void MemoryDB::RefreshUUIDCacheInternal() {
        // Caller must hold exclusive lock
        m_formIdToUUID.clear();
        m_uuidToFormId.clear();
        m_uuidToName.clear();
        m_formIdToBioTemplate.clear();
        m_bioSummaryCache.clear();

        if (!m_db) return;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT uuid, form_id, actor_name, bio_template_name FROM uuid_mappings";

        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            logger::warn("MemoryDB: Failed to prepare uuid_mappings query: {}",
                sqlite3_errmsg(m_db));
            return;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t uuid = sqlite3_column_int64(stmt, 0);
            int64_t formId64 = sqlite3_column_int64(stmt, 1);
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            const char* bioTemplate = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            auto fid = static_cast<RE::FormID>(formId64);
            m_formIdToUUID[fid] = uuid;
            m_uuidToFormId[uuid] = fid;
            if (name) {
                m_uuidToName[uuid] = name;
            }
            if (bioTemplate && bioTemplate[0]) {
                m_formIdToBioTemplate[fid] = bioTemplate;
            }
        }

        sqlite3_finalize(stmt);
        logger::info("MemoryDB: Cached {} UUID mappings, {} bio templates",
            m_formIdToUUID.size(), m_formIdToBioTemplate.size());
    }

    int64_t MemoryDB::FormIdToUUID(RE::FormID formId) const {
        auto it = m_formIdToUUID.find(formId);
        return (it != m_formIdToUUID.end()) ? it->second : 0;
    }

    std::string MemoryDB::UUIDToName(int64_t uuid) const {
        if (uuid == 0) return "";
        auto it = m_uuidToName.find(uuid);
        return (it != m_uuidToName.end()) ? it->second : "";
    }

    std::string MemoryDB::GetNPCBioSummary(RE::FormID formId) {
        // Check cache first (includes negative cache — empty string = no bio file)
        {
            std::shared_lock lock(m_mutex);
            auto cacheIt = m_bioSummaryCache.find(formId);
            if (cacheIt != m_bioSummaryCache.end()) {
                return cacheIt->second;
            }
        }

        // Look up bio_template_name
        std::string bioTemplate;
        {
            std::shared_lock lock(m_mutex);
            auto it = m_formIdToBioTemplate.find(formId);
            if (it == m_formIdToBioTemplate.end()) {
                // No bio template — cache empty result
                std::unique_lock wlock(m_mutex);
                m_bioSummaryCache[formId] = "";
                return "";
            }
            bioTemplate = it->second;
        }

        // Read the .prompt file from SkyrimNet's character prompts directory.
        // MO2's USVFS makes overwrite/mod files accessible via Data/ path.
        std::string filePath = "Data/SKSE/Plugins/SkyrimNet/prompts/characters/" + bioTemplate + ".prompt";

        std::string summary;
        std::error_code ec;
        if (std::filesystem::exists(filePath, ec)) {
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
                        // Trim whitespace
                        while (!summary.empty() && (summary.front() == ' ' || summary.front() == '\n' || summary.front() == '\r'))
                            summary.erase(summary.begin());
                        while (!summary.empty() && (summary.back() == ' ' || summary.back() == '\n' || summary.back() == '\r'))
                            summary.pop_back();
                    }
                }
            }
        }

        if (summary.empty()) {
            logger::debug("MemoryDB: No bio summary for FormID 0x{:08X} (template: '{}')", formId, bioTemplate);
        }

        // Cache result (including empty = negative cache)
        {
            std::unique_lock lock(m_mutex);
            m_bioSummaryCache[formId] = summary;
        }

        return summary;
    }

    // =========================================================================
    // GetFormattedMemories
    // =========================================================================

    std::string MemoryDB::GetFormattedMemories(RE::FormID formId, int maxCount) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        if (!m_db) return "";

        int64_t uuid = FormIdToUUID(formId);
        if (uuid == 0) return "";

        float currentHours = GetDBCurrentHoursLocked();

        // Most recent memories first — the DM needs to know what the NPC has been
        // doing LATELY, not their most "important" memory from thousands of days ago.
        // Query ALL UUIDs for this actor (NPCs can have multiple UUIDs across sessions).
        const char* sql =
            "SELECT m.content, m.emotion, m.importance_score, m.memory_type, m.game_time "
            "FROM memories m "
            "WHERE m.actor_uuid IN ("
            "  SELECT u.uuid FROM uuid_mappings u "
            "  WHERE u.actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            ") "
            "ORDER BY m.game_time DESC "
            "LIMIT ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return "";

        sqlite3_bind_int64(stmt, 1, uuid);
        sqlite3_bind_int(stmt, 2, maxCount);

        std::string result;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* emotion = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            const char* memType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            double gameTimeHours = sqlite3_column_double(stmt, 4);

            if (!content) continue;

            std::string sanitized = SanitizeForPrompt(content);
            std::string relTime = FormatRelativeTime(
                static_cast<float>(gameTimeHours), currentHours);

            if (!result.empty()) result += "\n";
            result += "- [";
            if (memType) result += memType;
            if (emotion && emotion[0] != '\0') {
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
    }

    // =========================================================================
    // GetFormattedRecentEvents
    // =========================================================================

    std::string MemoryDB::GetFormattedRecentEvents(int maxCount, const std::string& eventTypeFilter) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        if (!m_db) return "";

        // Build SQL with optional event_type filter
        std::string sql;
        std::vector<std::string> typeFilters;

        if (!eventTypeFilter.empty()) {
            // Split comma-separated filter
            std::istringstream ss(eventTypeFilter);
            std::string token;
            while (std::getline(ss, token, ',')) {
                // Trim whitespace
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    typeFilters.push_back(token.substr(start, end - start + 1));
                }
            }

            std::string whereClause = " WHERE event_type IN (";
            for (size_t i = 0; i < typeFilters.size(); ++i) {
                if (i > 0) whereClause += ",";
                whereClause += "?";
            }
            whereClause += ")";

            sql = "SELECT event_type, event_data, originating_actor_UUID, "
                  "target_actor_UUID, game_time, location "
                  "FROM events" + whereClause + " ORDER BY game_time DESC LIMIT ?";
        } else {
            sql = "SELECT event_type, event_data, originating_actor_UUID, "
                  "target_actor_UUID, game_time, location "
                  "FROM events ORDER BY game_time DESC LIMIT ?";
        }

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            logger::debug("MemoryDB: Failed to prepare events query: {}", sqlite3_errmsg(m_db));
            return "";
        }

        int paramIdx = 1;
        for (const auto& type : typeFilters) {
            sqlite3_bind_text(stmt, paramIdx++, type.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_int(stmt, paramIdx, maxCount);

        std::string result;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* eventType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* eventData = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int64_t originUUID = sqlite3_column_int64(stmt, 2);
            int64_t targetUUID = sqlite3_column_int64(stmt, 3);
            double gameTimeHours = sqlite3_column_double(stmt, 4);
            const char* location = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));

            if (!eventData) continue;

            // Format time as HH:MM AM/PM (game_time is in seconds)
            double secondOfDay = std::fmod(gameTimeHours, 86400.0);  // 86400 sec/day
            double hourOfDay = secondOfDay / SECONDS_PER_HOUR;
            int hour = static_cast<int>(hourOfDay);
            int minute = static_cast<int>((secondOfDay - hour * 3600.0) / 60.0);
            bool isPM = hour >= 12;
            int displayHour = hour % 12;
            if (displayHour == 0) displayHour = 12;

            std::string originName = UUIDToName(originUUID);
            std::string targetName = UUIDToName(targetUUID);

            std::string displayText = ExtractEventDisplayText(
                eventType ? eventType : "", eventData);
            displayText = Truncate(SanitizeForPrompt(displayText), 100);

            if (!result.empty()) result += "\n";
            result += fmt::format("- [{:d}:{:02d} {}] ",
                displayHour, minute, isPM ? "PM" : "AM");

            std::string typeStr = eventType ? eventType : "";
            if (typeStr == "dialogue" || typeStr == "dialogue_background" ||
                typeStr == "dialogue_npc" || typeStr == "dialogue_player" ||
                typeStr == "dialogue_player_text") {
                if (!originName.empty() && !targetName.empty()) {
                    result += originName + " told " + targetName + ": \"" + displayText + "\"";
                } else if (!originName.empty()) {
                    result += originName + ": \"" + displayText + "\"";
                } else {
                    result += displayText;
                }
            } else if (typeStr == "direct_narration") {
                result += displayText;
            } else if (typeStr == "death") {
                result += displayText;
            } else {
                if (!originName.empty()) {
                    result += originName + ": " + displayText;
                } else {
                    result += displayText;
                }
            }
        }

        sqlite3_finalize(stmt);
        return result;
    }

    // =========================================================================
    // GetRecentEventsForActor
    // =========================================================================

    std::string MemoryDB::GetRecentEventsForActor(RE::FormID formId, int maxCount) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        if (!m_db) return "";

        int64_t npcUUID = FormIdToUUID(formId);
        if (npcUUID == 0) return "";

        // Query recent story-relevant events involving this NPC.
        // Uses ALL UUIDs for this actor (NPCs can have multiple UUIDs across sessions).
        const char* sql =
            "SELECT event_type, event_data, originating_actor_UUID, "
            "target_actor_UUID, game_time "
            "FROM events "
            "WHERE (originating_actor_UUID IN ("
            "    SELECT u.uuid FROM uuid_mappings u "
            "    WHERE u.actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "  ) OR target_actor_UUID IN ("
            "    SELECT u.uuid FROM uuid_mappings u "
            "    WHERE u.actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "  )) "
            "  AND event_type IN ('direct_narration','custom_action','dialogue','persistent_generic') "
            "ORDER BY game_time DESC LIMIT ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return "";

        sqlite3_bind_int64(stmt, 1, npcUUID);
        sqlite3_bind_int64(stmt, 2, npcUUID);
        sqlite3_bind_int(stmt, 3, maxCount);

        float currentHours = GetDBCurrentHoursLocked();
        std::string result;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* eventType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* eventData = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int64_t originUUID = sqlite3_column_int64(stmt, 2);
            int64_t targetUUID = sqlite3_column_int64(stmt, 3);
            double gameTimeHours = sqlite3_column_double(stmt, 4);

            if (!eventData) continue;

            std::string displayText = ExtractEventDisplayText(
                eventType ? eventType : "", eventData);
            displayText = Truncate(SanitizeForPrompt(displayText), 80);
            if (displayText.empty()) continue;

            std::string timeAgo = FormatRelativeTime(
                static_cast<float>(gameTimeHours), currentHours);

            std::string originName = UUIDToName(originUUID);
            std::string targetName = UUIDToName(targetUUID);

            if (!result.empty()) result += "; ";
            result += "[" + timeAgo + "] ";

            std::string typeStr = eventType ? eventType : "";
            if (typeStr == "dialogue") {
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
    }

    // =========================================================================
    // GetActiveStoryNPCs
    // =========================================================================

    std::string MemoryDB::GetActiveStoryNPCs(int maxCount) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        if (!m_db) return "";

        float currentHours = GetDBCurrentHoursLocked();

        // Score NPCs by weighted memory + event activity
        // Recent (24h game) events count 3x, recent week 1x, older 0.3x
        // DB game_time is in seconds: 24h = 86400s, 168h = 604800s
        const char* sql =
            "SELECT u.actor_name, "
            "  COALESCE(m.mem_score, 0) + COALESCE(e.evt_score, 0) AS total_score "
            "FROM uuid_mappings u "
            "LEFT JOIN ("
            "  SELECT actor_uuid, "
            "    SUM(CASE WHEN (? - game_time) < 86400.0 THEN 3.0 * importance_score "
            "             WHEN (? - game_time) < 604800.0 THEN importance_score "
            "             ELSE 0.3 * importance_score END) AS mem_score "
            "  FROM memories GROUP BY actor_uuid"
            ") m ON m.actor_uuid = u.uuid "
            "LEFT JOIN ("
            "  SELECT originating_actor_UUID AS uuid, "
            "    SUM(CASE WHEN (? - game_time) < 86400.0 THEN 3.0 "
            "             WHEN (? - game_time) < 604800.0 THEN 1.0 "
            "             ELSE 0.3 END) AS evt_score "
            "  FROM events GROUP BY originating_actor_UUID"
            ") e ON e.uuid = u.uuid "
            "WHERE u.form_id != 20 "  // Exclude player (0x14 = 20)
            "  AND (m.mem_score IS NOT NULL OR e.evt_score IS NOT NULL) "
            "ORDER BY total_score DESC "
            "LIMIT ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return "";

        sqlite3_bind_double(stmt, 1, currentHours);
        sqlite3_bind_double(stmt, 2, currentHours);
        sqlite3_bind_double(stmt, 3, currentHours);
        sqlite3_bind_double(stmt, 4, currentHours);
        sqlite3_bind_int(stmt, 5, maxCount);

        std::string result;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (!name || name[0] == '\0') continue;

            if (!result.empty()) result += ", ";
            result += name;
        }

        return result;
    }

    // =========================================================================
    // GetRelationshipSummary
    // =========================================================================

    std::string MemoryDB::GetRelationshipSummary(RE::FormID formId1, RE::FormID formId2) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        if (!m_db) return "";

        int64_t uuid1 = FormIdToUUID(formId1);
        int64_t uuid2 = FormIdToUUID(formId2);
        if (uuid1 == 0 || uuid2 == 0) return "";

        // Query events involving both actors (all UUIDs for each actor).
        const char* sql =
            "SELECT event_type, event_data, game_time "
            "FROM events "
            "WHERE (originating_actor_UUID IN ("
            "    SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "  ) AND target_actor_UUID IN ("
            "    SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "  )) "
            "   OR (originating_actor_UUID IN ("
            "    SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "  ) AND target_actor_UUID IN ("
            "    SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "  )) "
            "ORDER BY game_time DESC LIMIT 5";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return "";

        sqlite3_bind_int64(stmt, 1, uuid1);
        sqlite3_bind_int64(stmt, 2, uuid2);
        sqlite3_bind_int64(stmt, 3, uuid2);
        sqlite3_bind_int64(stmt, 4, uuid1);

        float currentHours = GetDBCurrentHoursLocked();
        std::string result;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* eventType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* eventData = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            double gameTimeHours = sqlite3_column_double(stmt, 2);

            if (!eventData) continue;

            std::string text = Truncate(SanitizeForPrompt(
                ExtractEventDisplayText(eventType ? eventType : "", eventData)), 80);
            std::string relTime = FormatRelativeTime(
                static_cast<float>(gameTimeHours), currentHours);

            if (!result.empty()) result += "\n";
            result += "- " + text + " (" + relTime + ")";
        }

        // Also check memories that reference the other actor (all UUIDs)
        std::string name1 = UUIDToName(uuid1);
        const char* memSql =
            "SELECT content, game_time FROM memories "
            "WHERE actor_uuid IN ("
            "  SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            ") AND related_actors LIKE ? "
            "ORDER BY importance_score DESC LIMIT 3";

        auto* memStmt = PrepareOrGet(memSql);
        if (memStmt) {
            // Match any UUID for actor2 in the related_actors field
            std::string name2 = UUIDToName(uuid2);
            std::string namePattern = "%" + name2 + "%";
            sqlite3_bind_int64(memStmt, 1, uuid1);
            sqlite3_bind_text(memStmt, 2, namePattern.c_str(), -1, SQLITE_TRANSIENT);

            while (sqlite3_step(memStmt) == SQLITE_ROW) {
                const char* content = reinterpret_cast<const char*>(sqlite3_column_text(memStmt, 0));
                if (!content) continue;

                if (!result.empty()) result += "\n";
                result += "- " + name1 + " remembers: " +
                    Truncate(SanitizeForPrompt(content), 80);
            }
        }

        return result;
    }

    // =========================================================================
    // Helper Functions
    // =========================================================================

    float MemoryDB::GetDBCurrentHoursLocked() {
        // Use the DB's own time reference instead of Calendar.
        // SkyrimNet's game_time may not match Calendar::GetCurrentGameTime() * 24
        // (e.g., timescale mods, session-accumulated time, epoch differences).
        // MAX(game_time) from events is always consistent with memory/event timestamps.
        if (!m_db) return 0.0f;

        const char* sql = "SELECT MAX(game_time) FROM events";
        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return 0.0f;

        if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
            return static_cast<float>(sqlite3_column_double(stmt, 0));
        }

        // Fallback: use Calendar if DB has no events yet
        // GetCurrentGameTime() returns days; DB stores seconds (days * 86400)
        auto* cal = RE::Calendar::GetSingleton();
        return cal ? cal->GetCurrentGameTime() * 86400.0f : 0.0f;
    }

    float MemoryDB::GetCurrentDBHours() {
        EnsureConnected();
        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        return GetDBCurrentHoursLocked();
    }

    std::string MemoryDB::FormatRelativeTime(float gameTimeSeconds, float currentSecondsParam) {
        // Both parameters are in game-seconds (DB native unit).
        // Convert difference to hours for human-readable thresholds.
        float hoursAgo = (currentSecondsParam - gameTimeSeconds) / SECONDS_PER_HOUR;

        if (hoursAgo < 0.1f)   return "just now";        // < 6 minutes
        if (hoursAgo < 0.25f)  return "minutes ago";      // < 15 minutes
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

    std::string MemoryDB::ExtractEventDisplayText(const std::string& eventType, const char* eventData) {
        if (!eventData) return "";
        std::string data(eventData);

        // Helper to extract a JSON string field value
        auto extractField = [&](const char* field) -> std::string {
            std::string needle = std::string("\"") + field + "\":\"";
            auto pos = data.find(needle);
            if (pos == std::string::npos) return "";
            auto start = pos + needle.size();
            auto end = start;
            while (end < data.size()) {
                if (data[end] == '"' && (end == 0 || data[end - 1] != '\\')) break;
                ++end;
            }
            return data.substr(start, end - start);
        };

        // Extract based on known event_data JSON structures
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
        if (data.empty() || data[0] != '{') {
            return data;  // Plain text
        }

        // Unknown JSON format — try common field names
        std::string text = extractField("text");
        if (text.empty()) text = extractField("content");
        if (text.empty()) text = extractField("msg");
        if (text.empty()) text = extractField("narration");
        return text.empty() ? data : text;
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
                case '\r': break;  // Skip carriage returns
                case '\t': result += "\\t";  break;
                default:   result += c;      break;
            }
        }
        return result;
    }

    // =========================================================================
    // Story Candidate Selection Queries
    //
    // DUAL UUID / STALE FORMID BUG (documented 2026-02):
    // SkyrimNet assigns new UUIDs to actors across game sessions. The same NPC
    // can accumulate multiple UUIDs in the DB, each with a potentially different
    // form_id (stale from mod updates or base-form vs reference mismatches).
    //
    // Example from production DB:
    //   Player "Galanx": UUID 3734... (126 events) + UUID 4819... (2407 events)
    //   Heidi: DB form_id=0xBC027633, actual game FormID=0xBC018519
    //   Daegon: DB form_id=0xBB005902, actual game FormID=0xBB005900
    //
    // WORKAROUND: All candidate queries GROUP BY actor_name (not UUID or form_id)
    // to aggregate scores across all UUID aliases. Callers resolve Actor* via
    // NPCIndex::ResolveFromMemoryDB() which tries FormID first, then falls back
    // to FindByName() using the reliable display name from uuid_mappings.
    //
    // Player interaction queries use a UUID subquery instead of a single UUID:
    //   WHERE uuid IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20)
    // This captures ALL player UUIDs regardless of how many exist.
    // =========================================================================

    std::vector<RankedCandidate> MemoryDB::GetRankedCandidateFormIDs(int maxCount) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        std::vector<RankedCandidate> result;
        if (!m_db) return result;

        float currentHours = GetDBCurrentHoursLocked();

        // GROUP BY actor_name to aggregate scores across UUID aliases (see bug note above)
        // DB game_time is in seconds: 24h = 86400s, 168h = 604800s
        // Events subquery: only count player-involved events (form_id=20).
        // NPC-to-NPC social events should NOT boost candidate ranking — old friends
        // with deep player history must not be crowded out by background social chatter.
        const char* sql =
            "SELECT u.actor_name, MAX(u.form_id) AS form_id, "
            "  SUM(COALESCE(m.mem_score, 0) + COALESCE(e.evt_score, 0)) AS total_score "
            "FROM uuid_mappings u "
            "LEFT JOIN ("
            "  SELECT actor_uuid, "
            "    SUM(CASE WHEN (? - game_time) < 86400.0 THEN 3.0 * importance_score "
            "             WHEN (? - game_time) < 604800.0 THEN importance_score "
            "             ELSE 0.3 * importance_score END) AS mem_score "
            "  FROM memories GROUP BY actor_uuid"
            ") m ON m.actor_uuid = u.uuid "
            "LEFT JOIN ("
            "  SELECT "
            "    CASE WHEN e.originating_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "      THEN e.target_actor_UUID ELSE e.originating_actor_UUID END AS uuid, "
            "    SUM(CASE WHEN (? - e.game_time) < 86400.0 THEN 3.0 "
            "             WHEN (? - e.game_time) < 604800.0 THEN 1.0 "
            "             ELSE 0.3 END) AS evt_score "
            "  FROM events e "
            "  WHERE (e.originating_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "      OR e.target_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20)) "
            "    AND e.originating_actor_UUID != e.target_actor_UUID "
            "  GROUP BY uuid"
            ") e ON e.uuid = u.uuid "
            "WHERE u.form_id != 20 "
            "  AND (m.mem_score IS NOT NULL OR e.evt_score IS NOT NULL) "
            "GROUP BY u.actor_name "
            "ORDER BY total_score DESC "
            "LIMIT ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return result;

        sqlite3_bind_double(stmt, 1, currentHours);
        sqlite3_bind_double(stmt, 2, currentHours);
        sqlite3_bind_double(stmt, 3, currentHours);
        sqlite3_bind_double(stmt, 4, currentHours);
        sqlite3_bind_int(stmt, 5, maxCount);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t formId64 = sqlite3_column_int64(stmt, 1);
            double score = sqlite3_column_double(stmt, 2);
            if (name && formId64 > 0) {
                result.push_back({static_cast<RE::FormID>(formId64), name, static_cast<float>(score)});
            }
        }

        logger::debug("MemoryDB: GetRankedCandidateFormIDs returned {} candidates", result.size());
        return result;
    }

    std::vector<RankedCandidate> MemoryDB::GetRelatedCandidateFormIDs(RE::FormID actorFormId, int maxCount) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        std::vector<RankedCandidate> result;
        if (!m_db) return result;

        int64_t actorUUID = FormIdToUUID(actorFormId);
        if (actorUUID == 0) return result;

        float currentHours = GetDBCurrentHoursLocked();

        // Returns NPCs related to the anchor actor via shared events.
        // Uses ALL UUIDs for the anchor (handles multi-session UUID splits).
        const char* sql =
            "SELECT "
            "  re.related_name, "
            "  (SELECT MAX(form_id) FROM uuid_mappings WHERE actor_name = re.related_name) AS related_form_id, "
            "  SUM(re.event_score) AS relation_score "
            "FROM ("
            "  SELECT "
            "    CASE WHEN e.originating_actor_UUID IN ("
            "      SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "    ) THEN u2.actor_name ELSE u1.actor_name END AS related_name, "
            "    CASE WHEN (? - e.game_time) < 86400.0 THEN 3.0 "
            "         WHEN (? - e.game_time) < 604800.0 THEN 1.0 "
            "         ELSE 0.3 END AS event_score "
            "  FROM events e "
            "  JOIN uuid_mappings u1 ON u1.uuid = e.originating_actor_UUID "
            "  JOIN uuid_mappings u2 ON u2.uuid = e.target_actor_UUID "
            "  WHERE (e.originating_actor_UUID IN ("
            "      SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "    ) OR e.target_actor_UUID IN ("
            "      SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "    )) "
            "    AND e.originating_actor_UUID IS NOT NULL "
            "    AND e.target_actor_UUID IS NOT NULL "
            "    AND e.originating_actor_UUID != e.target_actor_UUID "
            ") re "
            "GROUP BY re.related_name "
            "ORDER BY relation_score DESC "
            "LIMIT ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return result;

        sqlite3_bind_int64(stmt, 1, actorUUID);
        sqlite3_bind_double(stmt, 2, currentHours);
        sqlite3_bind_double(stmt, 3, currentHours);
        sqlite3_bind_int64(stmt, 4, actorUUID);
        sqlite3_bind_int64(stmt, 5, actorUUID);
        sqlite3_bind_int(stmt, 6, maxCount);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t formId64 = sqlite3_column_int64(stmt, 1);
            double score = sqlite3_column_double(stmt, 2);
            if (name && formId64 > 0 && formId64 != 20) {
                result.push_back({static_cast<RE::FormID>(formId64), name, static_cast<float>(score)});
            }
        }

        logger::debug("MemoryDB: GetRelatedCandidateFormIDs for 0x{:08X} returned {} candidates",
            actorFormId, result.size());
        return result;
    }

    std::unordered_set<std::string> MemoryDB::GetRecentPlayerInteractionNames(float withinHours) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        std::unordered_set<std::string> result;
        if (!m_db) return result;

        float currentHours = GetDBCurrentHoursLocked();

        // Returns DISTINCT lowercase NPC names who the player ACTIVELY conversed with recently.
        // Only counts dialogue_player_text (player's own typed responses), NOT 'dialogue'
        // (NPC-initiated greetings/ambient lines). The old filter included 'dialogue' which
        // excluded 100+ NPCs (every NPC who greeted the player), starving the candidate pool
        // of all MemoryDB-ranked candidates and filling it with random padding strangers.
        const char* sql =
            "SELECT DISTINCT LOWER("
            "  CASE WHEN e.originating_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "    THEN u2.actor_name ELSE u1.actor_name END"
            ") AS npc_name "
            "FROM events e "
            "JOIN uuid_mappings u1 ON u1.uuid = e.originating_actor_UUID "
            "JOIN uuid_mappings u2 ON u2.uuid = e.target_actor_UUID "
            "WHERE (e.originating_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "    OR e.target_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20)) "
            "  AND e.originating_actor_UUID != e.target_actor_UUID "
            "  AND e.event_type = 'dialogue_player_text' "
            "  AND (? - e.game_time) < ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return result;

        sqlite3_bind_double(stmt, 1, currentHours);
        sqlite3_bind_double(stmt, 2, withinHours * SECONDS_PER_HOUR);  // Convert hours→seconds for DB

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (name && name[0]) {
                result.insert(name);  // Already lowercase from LOWER() in SQL
            }
        }

        logger::debug("MemoryDB: {} NPCs interacted with player in last {:.0f}h",
            result.size(), withinHours);
        return result;
    }

    std::vector<PlayerRelationship> MemoryDB::GetPlayerRelationshipData() {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        std::vector<PlayerRelationship> result;
        if (!m_db) return result;

        // Returns interaction count + last interaction time per NPC.
        // Uses UUID subquery for player to catch ALL player UUIDs (dual UUID bug).
        // Uses inner subquery to isolate per-row CASE from per-group aggregation.
        // BUG FIX: Original CASE+MAX pattern mixed per-row direction detection with
        // per-group MAX, potentially returning player's form_id for NPC entries when
        // events exist in both directions (Player→NPC and NPC→Player).
        const char* sql =
            "SELECT "
            "  ne.npc_name, "
            "  (SELECT MAX(form_id) FROM uuid_mappings WHERE actor_name = ne.npc_name) AS npc_form_id, "
            "  COUNT(*) AS interaction_count, "
            "  MAX(ne.game_time) AS last_interaction "
            "FROM ("
            "  SELECT "
            "    CASE WHEN e.originating_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "      THEN u2.actor_name ELSE u1.actor_name END AS npc_name, "
            "    e.game_time "
            "  FROM events e "
            "  JOIN uuid_mappings u1 ON u1.uuid = e.originating_actor_UUID "
            "  JOIN uuid_mappings u2 ON u2.uuid = e.target_actor_UUID "
            "  WHERE (e.originating_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "      OR e.target_actor_UUID IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20)) "
            "    AND e.originating_actor_UUID != e.target_actor_UUID "
            ") ne "
            "GROUP BY ne.npc_name";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return result;

        // No bind params — player UUIDs resolved via subquery

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t formId64 = sqlite3_column_int64(stmt, 1);
            int count = sqlite3_column_int(stmt, 2);
            float lastHours = static_cast<float>(sqlite3_column_double(stmt, 3));

            if (name && name[0] && formId64 > 0 && formId64 != 20) {
                result.push_back({
                    static_cast<RE::FormID>(formId64),
                    name,
                    count,
                    lastHours
                });
            }
        }

        logger::debug("MemoryDB: GetPlayerRelationshipData returned {} NPCs", result.size());
        return result;
    }

    std::vector<RankedCandidate> MemoryDB::GetSociallyActiveFormIDs(int maxCount) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        std::vector<RankedCandidate> result;
        if (!m_db) return result;

        // Rank NPCs by NPC-to-NPC event count (excluding player as either party).
        // Excludes ALL player UUIDs via subquery (see dual UUID bug note above).
        // GROUP BY actor_name handles UUID aliases for the same NPC.
        const char* sql =
            "SELECT u.actor_name, MAX(u.form_id) AS form_id, COUNT(*) AS social_score "
            "FROM events e "
            "JOIN uuid_mappings u ON u.uuid = e.originating_actor_UUID "
            "WHERE e.originating_actor_UUID != e.target_actor_UUID "
            "  AND e.originating_actor_UUID NOT IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "  AND e.target_actor_UUID NOT IN (SELECT uuid FROM uuid_mappings WHERE form_id = 20) "
            "  AND u.form_id != 20 "
            "GROUP BY u.actor_name "
            "ORDER BY social_score DESC "
            "LIMIT ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return result;

        sqlite3_bind_int(stmt, 1, maxCount);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t formId64 = sqlite3_column_int64(stmt, 1);
            float score = static_cast<float>(sqlite3_column_int(stmt, 2));
            if (name && formId64 > 0) {
                result.push_back({static_cast<RE::FormID>(formId64), name, score});
            }
        }

        logger::debug("MemoryDB: GetSociallyActiveFormIDs returned {} NPCs", result.size());
        return result;
    }

    std::vector<NPCPairRelationship> MemoryDB::GetPoolRelationships(
        const std::vector<RE::FormID>& poolFormIds) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        std::vector<NPCPairRelationship> result;
        if (!m_db || poolFormIds.size() < 2) return result;

        // Convert pool FormIDs to ALL UUIDs per actor (handles multi-session splits).
        // For each pool FormID, find the actor name, then include all UUIDs with that name.
        std::string uuidList;
        std::unordered_map<int64_t, RE::FormID> uuidToFormId;
        for (auto fid : poolFormIds) {
            if (fid == 0x14) continue;
            int64_t primaryUUID = FormIdToUUID(fid);
            if (primaryUUID == 0) continue;
            std::string name = UUIDToName(primaryUUID);
            if (name.empty()) {
                // Fallback: just use the primary UUID
                if (!uuidList.empty()) uuidList += ",";
                uuidList += std::to_string(primaryUUID);
                uuidToFormId[primaryUUID] = fid;
                continue;
            }
            // Find all UUIDs with this actor name
            for (const auto& [uuid, uuidName] : m_uuidToName) {
                if (uuidName == name) {
                    if (!uuidList.empty()) uuidList += ",";
                    uuidList += std::to_string(uuid);
                    uuidToFormId[uuid] = fid;
                }
            }
        }

        if (uuidList.empty()) return result;

        // Query directed event pairs within the pool (both A→B and B→A)
        std::string sql =
            "SELECT e.originating_actor_UUID, e.target_actor_UUID, COUNT(*) AS shared "
            "FROM events e "
            "WHERE e.originating_actor_UUID IN (" + uuidList + ") "
            "  AND e.target_actor_UUID IN (" + uuidList + ") "
            "  AND e.originating_actor_UUID != e.target_actor_UUID "
            "GROUP BY e.originating_actor_UUID, e.target_actor_UUID "
            "HAVING shared > 3";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            logger::debug("MemoryDB: Failed to prepare pool relationships query: {}",
                sqlite3_errmsg(m_db));
            return result;
        }

        // Merge directed pairs: A→B + B→A into one undirected pair
        struct PairKey {
            RE::FormID lo, hi;
            bool operator==(const PairKey& o) const { return lo == o.lo && hi == o.hi; }
        };
        struct PairHash {
            size_t operator()(const PairKey& k) const {
                return std::hash<uint64_t>{}(
                    (static_cast<uint64_t>(k.lo) << 32) | k.hi);
            }
        };
        std::unordered_map<PairKey, int, PairHash> merged;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t uuid1 = sqlite3_column_int64(stmt, 0);
            int64_t uuid2 = sqlite3_column_int64(stmt, 1);
            int count = sqlite3_column_int(stmt, 2);

            auto it1 = uuidToFormId.find(uuid1);
            auto it2 = uuidToFormId.find(uuid2);
            if (it1 == uuidToFormId.end() || it2 == uuidToFormId.end()) continue;

            RE::FormID fid1 = it1->second;
            RE::FormID fid2 = it2->second;
            PairKey key{std::min(fid1, fid2), std::max(fid1, fid2)};
            merged[key] += count;
        }
        sqlite3_finalize(stmt);

        for (auto& [key, count] : merged) {
            result.push_back({key.lo, key.hi, count});
        }

        // Sort by shared events descending
        std::sort(result.begin(), result.end(),
            [](const NPCPairRelationship& a, const NPCPairRelationship& b) {
                return a.sharedEvents > b.sharedEvents;
            });

        logger::debug("MemoryDB: GetPoolRelationships found {} pairs", result.size());
        return result;
    }

    // =========================================================================
    // Dialogue Safety Net Functions
    // =========================================================================

    std::string MemoryDB::GetRecentDialogueForActor(RE::FormID formId, int maxExchanges) {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        if (!m_db) return "";

        int64_t npcUUID = FormIdToUUID(formId);
        int64_t playerUUID = FormIdToUUID(0x14);
        if (npcUUID == 0 || playerUUID == 0) return "";

        float currentHours = GetDBCurrentHoursLocked();
        float recentThreshold = currentHours - 12.0f * SECONDS_PER_HOUR;  // Last 12 game-hours (covers 2h+ tick interval)

        std::string npcName = UUIDToName(npcUUID);
        std::string playerName = UUIDToName(playerUUID);
        if (npcName.empty()) npcName = "NPC";
        if (playerName.empty()) playerName = "Player";

        // Use ALL UUIDs for both NPC and player (handles multi-session UUID splits).
        const char* sql =
            "SELECT event_type, event_data, originating_actor_UUID "
            "FROM events "
            "WHERE ((event_type = 'dialogue' "
            "    AND originating_actor_UUID IN ("
            "      SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "    ) AND target_actor_UUID IN ("
            "      SELECT uuid FROM uuid_mappings WHERE form_id = 20"
            "    )) "
            "  OR (event_type = 'dialogue_player_text' "
            "    AND target_actor_UUID IN ("
            "      SELECT uuid FROM uuid_mappings WHERE actor_name = (SELECT actor_name FROM uuid_mappings WHERE uuid = ?)"
            "    ))) "
            "  AND game_time > ? "
            "ORDER BY game_time DESC "
            "LIMIT ?";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return "";

        sqlite3_bind_int64(stmt, 1, npcUUID);
        sqlite3_bind_int64(stmt, 2, npcUUID);
        sqlite3_bind_double(stmt, 3, recentThreshold);
        sqlite3_bind_int(stmt, 4, maxExchanges * 2);

        // Build set of all player UUIDs for speaker identification
        std::unordered_set<int64_t> playerUUIDs;
        for (const auto& [uuid, name] : m_uuidToName) {
            if (m_uuidToFormId.count(uuid) && m_uuidToFormId.at(uuid) == 0x14) {
                playerUUIDs.insert(uuid);
            }
        }
        // Also check by name match (handles stale FormID mappings)
        if (!playerName.empty()) {
            for (const auto& [uuid, name] : m_uuidToName) {
                if (name == playerName) playerUUIDs.insert(uuid);
            }
        }

        // Collect in reverse (query returns DESC, we want chronological)
        std::vector<std::string> lines;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* eventType = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* eventData = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            int64_t originUUID = sqlite3_column_int64(stmt, 2);

            std::string text = ExtractEventDisplayText(
                eventType ? eventType : "", eventData);
            text = SanitizeForPrompt(text);
            if (text.empty()) continue;

            // Identify speaker
            std::string speaker = playerUUIDs.count(originUUID) ? playerName : npcName;
            lines.push_back(speaker + ": " + text);
        }

        // Reverse to chronological order
        std::reverse(lines.begin(), lines.end());

        // Join with newlines (raw text — caller escapes if needed)
        std::string result;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) result += "\n";
            result += lines[i];
        }

        return result;
    }

    LatestDialogueInfo MemoryDB::GetLatestDialogueInfo() {
        EnsureConnected();

        std::unique_lock lock(m_mutex);
        StmtGuard guard{m_stmtCache};
        if (!m_db) return {};

        // Use ALL player UUIDs (player can have multiple UUIDs across sessions).
        const char* sql =
            "SELECT originating_actor_UUID, game_time FROM events "
            "WHERE event_type = 'dialogue' AND target_actor_UUID IN ("
            "  SELECT uuid FROM uuid_mappings WHERE form_id = 20"
            ") "
            "ORDER BY game_time DESC LIMIT 1";

        auto* stmt = PrepareOrGet(sql);
        if (!stmt) return {};

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int64_t npcUUID = sqlite3_column_int64(stmt, 0);
            float gameTime = static_cast<float>(sqlite3_column_double(stmt, 1));
            auto it = m_uuidToFormId.find(npcUUID);
            if (it != m_uuidToFormId.end()) {
                return { it->second, gameTime };
            }
        }

        return {};
    }

    int MemoryDB::CheckScheduleKeywords(const std::string& text) {
        // Lowercase the text for case-insensitive matching
        std::string lower;
        lower.reserve(text.size());
        for (char c : text) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        auto contains = [&](const char* phrase) {
            return lower.find(phrase) != std::string::npos;
        };

        // --- Fetch/bring keywords (check first — more specific) ---
        if (contains("bring him") || contains("bring her") || contains("bring them") ||
            contains("fetch") || contains("go get") || contains("find and bring")) {
            return 2;
        }

        // --- Delivery keywords ---
        if (contains("tell him") || contains("tell her") || contains("tell them") ||
            contains("let him know") || contains("let her know") ||
            contains("send word") || contains("deliver a message") ||
            contains("message to") || contains("inform")) {
            return 3;
        }

        // --- Meeting keywords ---
        // Strong commitment phrases that alone indicate a meeting
        bool hasStrongCommitment =
            contains("see you tonight") || contains("see you tomorrow") ||
            contains("see you at") || contains("i'll be there") ||
            contains("until then") || contains("don't be late") ||
            contains("don't keep me waiting") || contains("expect you");

        if (hasStrongCommitment) return 1;

        // Time words
        bool hasTimeWord =
            contains("tonight") || contains("tomorrow") || contains("morning") ||
            contains("evening") || contains("afternoon") || contains("dawn") ||
            contains("dusk") || contains("sunset") || contains("sunrise") ||
            contains("noon") || contains("midnight") || contains("later") ||
            contains("soon") || contains("hour") || contains("nightfall") ||
            contains("first light") || contains("after dark") || contains("before dark");

        // Meeting/commitment phrases
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
