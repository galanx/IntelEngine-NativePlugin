/**
 * PoliticalDB Implementation
 *
 * SQLite wrapper for IntelEngine.db. All faction politics data stored here.
 * Independent from SkyrimNet's database.
 */

#include "PoliticalDB.h"
#include "FactionPolitics.h"  // for MAX_EVENT_DESCRIPTION_LENGTH, MAX_EVENT_REPLAY_LIMIT

#include <algorithm>
#include <filesystem>

namespace IntelEngine {

    // =========================================================================
    // Lifecycle
    // =========================================================================

    bool PoliticalDB::Initialize(const std::string& dbPath) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (db_) return true;  // Already open

        auto dir = std::filesystem::path(dbPath).parent_path();
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directories(dir);
        }

        int rc = sqlite3_open(dbPath.c_str(), &db_);
        if (rc != SQLITE_OK) {
            logger::error("PoliticalDB: Failed to open {}: {}", dbPath, sqlite3_errmsg(db_));
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }

        Execute("PRAGMA journal_mode=WAL");
        Execute("PRAGMA foreign_keys=ON");

        if (!CreateTables()) {
            logger::error("PoliticalDB: Failed to create tables");
            sqlite3_close(db_);
            db_ = nullptr;
            return false;
        }

        logger::info("PoliticalDB: Initialized at {}", dbPath);
        return true;
    }

    void PoliticalDB::Shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
            logger::info("PoliticalDB: Shutdown");
        }
    }

    bool PoliticalDB::CreateTables() {
        // Note: war_start_time in faction_relations and total_contributions in
        // player_faction_standing are legacy columns kept for schema compatibility
        // with existing databases. They are not read or written by current code.
        const char* schema = R"SQL(
            CREATE TABLE IF NOT EXISTS faction_relations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                faction_a TEXT NOT NULL,
                faction_b TEXT NOT NULL,
                relation_score INTEGER DEFAULT 0,
                trade_active BOOLEAN DEFAULT 0,
                war_active BOOLEAN DEFAULT 0,
                war_start_time REAL,
                last_event_time REAL,
                UNIQUE(faction_a, faction_b)
            );

            CREATE TABLE IF NOT EXISTS faction_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                faction_a TEXT NOT NULL,
                faction_b TEXT,
                event_type TEXT NOT NULL,
                description TEXT NOT NULL,
                relation_delta INTEGER DEFAULT 0,
                game_time REAL,
                instigator_npc TEXT
            );

            CREATE TABLE IF NOT EXISTS player_faction_standing (
                faction_id TEXT PRIMARY KEY,
                standing INTEGER DEFAULT 0,
                title TEXT,
                last_change_time REAL,
                total_contributions INTEGER DEFAULT 0
            );

            CREATE TABLE IF NOT EXISTS faction_wars (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                faction_a TEXT NOT NULL,
                faction_b TEXT NOT NULL,
                start_time REAL NOT NULL,
                end_time REAL,
                victor TEXT,
                battles_fought INTEGER DEFAULT 0,
                faction_a_morale INTEGER DEFAULT 100,
                faction_b_morale INTEGER DEFAULT 100,
                faction_a_strength INTEGER DEFAULT 100,
                faction_b_strength INTEGER DEFAULT 100,
                UNIQUE(faction_a, faction_b, start_time)
            );

            CREATE INDEX IF NOT EXISTS idx_events_game_time ON faction_events(game_time DESC);
            CREATE INDEX IF NOT EXISTS idx_events_faction ON faction_events(faction_a, faction_b);
            CREATE INDEX IF NOT EXISTS idx_wars_active ON faction_wars(end_time) WHERE end_time IS NULL;
        )SQL";

        return Execute(schema);
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    std::pair<std::string, std::string> PoliticalDB::OrderFactions(
        const std::string& a, const std::string& b) {
        return (a <= b) ? std::make_pair(a, b) : std::make_pair(b, a);
    }

    static std::string SafeColumnText(sqlite3_stmt* stmt, int col) {
        auto* text = sqlite3_column_text(stmt, col);
        return text ? reinterpret_cast<const char*>(text) : "";
    }

    static FactionEvent ReadEventRow(sqlite3_stmt* stmt) {
        FactionEvent e;
        e.id = sqlite3_column_int(stmt, 0);
        e.factionA = SafeColumnText(stmt, 1);
        e.factionB = SafeColumnText(stmt, 2);
        e.eventType = SafeColumnText(stmt, 3);
        e.description = SafeColumnText(stmt, 4);
        e.relationDelta = sqlite3_column_int(stmt, 5);
        e.gameTime = static_cast<float>(sqlite3_column_double(stmt, 6));
        e.instigatorNpc = SafeColumnText(stmt, 7);
        return e;
    }

    static FactionWar ReadWarRow(sqlite3_stmt* stmt) {
        FactionWar w;
        w.id = sqlite3_column_int(stmt, 0);
        w.factionA = SafeColumnText(stmt, 1);
        w.factionB = SafeColumnText(stmt, 2);
        w.startTime = static_cast<float>(sqlite3_column_double(stmt, 3));
        w.battlesFought = sqlite3_column_int(stmt, 4);
        w.factionAMorale = sqlite3_column_int(stmt, 5);
        w.factionBMorale = sqlite3_column_int(stmt, 6);
        w.factionAStrength = sqlite3_column_int(stmt, 7);
        w.factionBStrength = sqlite3_column_int(stmt, 8);
        return w;
    }

    bool PoliticalDB::Execute(const std::string& sql) {
        if (!db_) return false;
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            logger::error("PoliticalDB SQL error: {}", errMsg ? errMsg : "unknown");
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    bool PoliticalDB::UpsertRelationScore(const std::string& orderedA, const std::string& orderedB, int score) {
        const char* sql = R"SQL(
            INSERT INTO faction_relations (faction_a, faction_b, relation_score)
            VALUES (?, ?, ?)
            ON CONFLICT(faction_a, faction_b) DO UPDATE SET relation_score = excluded.relation_score
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, orderedA.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, orderedB.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, score);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    // =========================================================================
    // Faction Relations
    // =========================================================================

    int PoliticalDB::GetRelation(const std::string& factionA, const std::string& factionB) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;

        auto [a, b] = OrderFactions(factionA, factionB);

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT relation_score FROM faction_relations WHERE faction_a = ? AND faction_b = ?";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

        sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);

        int result = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    bool PoliticalDB::SetRelation(const std::string& factionA, const std::string& factionB, int score) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        auto [a, b] = OrderFactions(factionA, factionB);
        score = std::clamp(score, RELATION_MIN, RELATION_MAX);
        return UpsertRelationScore(a, b, score);
    }

    int PoliticalDB::AdjustRelation(const std::string& factionA, const std::string& factionB, int delta) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;

        auto [a, b] = OrderFactions(factionA, factionB);

        // Read current
        int current = 0;
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "SELECT relation_score FROM faction_relations WHERE faction_a = ? AND faction_b = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    current = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }

        int newScore = std::clamp(current + delta, RELATION_MIN, RELATION_MAX);

        if (UpsertRelationScore(a, b, newScore)) {
            return newScore;
        }
        return current;
    }

    std::vector<FactionRelation> PoliticalDB::GetAllRelations() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FactionRelation> results;
        if (!db_) return results;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT faction_a, faction_b, relation_score, trade_active, war_active FROM faction_relations";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            FactionRelation r;
            r.factionA = SafeColumnText(stmt, 0);
            r.factionB = SafeColumnText(stmt, 1);
            r.relationScore = sqlite3_column_int(stmt, 2);
            r.tradeActive = sqlite3_column_int(stmt, 3) != 0;
            r.warActive = sqlite3_column_int(stmt, 4) != 0;
            results.push_back(std::move(r));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    // =========================================================================
    // Faction Events
    // =========================================================================

    int PoliticalDB::RecordEvent(const std::string& factionA, const std::string& factionB,
                                  const std::string& eventType, const std::string& description,
                                  int relationDelta, float gameTime,
                                  const std::string& instigatorNpc) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return -1;

        // Truncate LLM-generated descriptions to prevent unbounded DB growth
        std::string safeDescription = description;
        if (safeDescription.size() > MAX_EVENT_DESCRIPTION_LENGTH) {
            safeDescription.resize(MAX_EVENT_DESCRIPTION_LENGTH);
            safeDescription += "...";
        }

        const char* sql = R"SQL(
            INSERT INTO faction_events (faction_a, faction_b, event_type, description, relation_delta, game_time, instigator_npc)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

        sqlite3_bind_text(stmt, 1, factionA.c_str(), -1, SQLITE_TRANSIENT);
        if (factionB.empty()) {
            sqlite3_bind_null(stmt, 2);
        } else {
            sqlite3_bind_text(stmt, 2, factionB.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_text(stmt, 3, eventType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, safeDescription.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, relationDelta);
        sqlite3_bind_double(stmt, 6, gameTime);
        sqlite3_bind_text(stmt, 7, instigatorNpc.c_str(), -1, SQLITE_TRANSIENT);

        int eventId = -1;
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            eventId = static_cast<int>(sqlite3_last_insert_rowid(db_));
        }
        sqlite3_finalize(stmt);

        // Update last_event_time on the relation row if both factions specified
        if (eventId >= 0 && !factionB.empty()) {
            auto [a, b] = OrderFactions(factionA, factionB);
            const char* updateSql = R"SQL(
                UPDATE faction_relations SET last_event_time = ? WHERE faction_a = ? AND faction_b = ?
            )SQL";
            sqlite3_stmt* updateStmt = nullptr;
            if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(updateStmt, 1, gameTime);
                sqlite3_bind_text(updateStmt, 2, a.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 3, b.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(updateStmt);
                sqlite3_finalize(updateStmt);
            }
        }

        return eventId;
    }

    std::vector<FactionEvent> PoliticalDB::GetRecentEvents(int maxCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FactionEvent> results;
        if (!db_) return results;

        const char* sql = R"SQL(
            SELECT id, faction_a, faction_b, event_type, description, relation_delta, game_time, instigator_npc
            FROM faction_events ORDER BY game_time DESC LIMIT ?
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

        sqlite3_bind_int(stmt, 1, maxCount);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(ReadEventRow(stmt));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<FactionEvent> PoliticalDB::GetAllEventsChronological() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FactionEvent> results;
        if (!db_) return results;

        const char* sql = R"SQL(
            SELECT id, faction_a, faction_b, event_type, description, relation_delta, game_time, instigator_npc
            FROM faction_events ORDER BY game_time ASC LIMIT ?
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

        sqlite3_bind_int(stmt, 1, MAX_EVENT_REPLAY_LIMIT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(ReadEventRow(stmt));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    // =========================================================================
    // Player Standing
    // =========================================================================

    int PoliticalDB::GetPlayerStanding(const std::string& factionId) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT standing FROM player_faction_standing WHERE faction_id = ?";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

        sqlite3_bind_text(stmt, 1, factionId.c_str(), -1, SQLITE_TRANSIENT);

        int result = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    int PoliticalDB::AdjustPlayerStanding(const std::string& factionId, int delta, float gameTime) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;

        // Read current
        int current = 0;
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "SELECT standing FROM player_faction_standing WHERE faction_id = ?";
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, factionId.c_str(), -1, SQLITE_TRANSIENT);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    current = sqlite3_column_int(stmt, 0);
                }
                sqlite3_finalize(stmt);
            }
        }

        int newStanding = std::clamp(current + delta, RELATION_MIN, RELATION_MAX);

        const char* sql = R"SQL(
            INSERT INTO player_faction_standing (faction_id, standing, last_change_time)
            VALUES (?, ?, ?)
            ON CONFLICT(faction_id) DO UPDATE SET standing = excluded.standing, last_change_time = excluded.last_change_time
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return current;

        sqlite3_bind_text(stmt, 1, factionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, newStanding);
        sqlite3_bind_double(stmt, 3, gameTime);

        if (sqlite3_step(stmt) == SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return newStanding;
        }
        sqlite3_finalize(stmt);
        return current;
    }

    std::vector<PlayerStanding> PoliticalDB::GetAllPlayerStandings() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<PlayerStanding> results;
        if (!db_) return results;

        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT faction_id, standing, title, last_change_time FROM player_faction_standing";
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            PlayerStanding ps;
            ps.factionId = SafeColumnText(stmt, 0);
            ps.standing = sqlite3_column_int(stmt, 1);
            ps.title = SafeColumnText(stmt, 2);
            ps.lastChangeTime = static_cast<float>(sqlite3_column_double(stmt, 3));
            results.push_back(std::move(ps));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    // =========================================================================
    // Wars
    // =========================================================================

    std::optional<FactionWar> PoliticalDB::GetActiveWar(const std::string& factionA, const std::string& factionB) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return std::nullopt;

        auto [a, b] = OrderFactions(factionA, factionB);

        const char* sql = R"SQL(
            SELECT id, faction_a, faction_b, start_time, battles_fought,
                   faction_a_morale, faction_b_morale, faction_a_strength, faction_b_strength
            FROM faction_wars WHERE faction_a = ? AND faction_b = ? AND end_time IS NULL
            ORDER BY start_time DESC LIMIT 1
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

        sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<FactionWar> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = ReadWarRow(stmt);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<FactionWar> PoliticalDB::GetActiveWars() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<FactionWar> results;
        if (!db_) return results;

        const char* sql = R"SQL(
            SELECT id, faction_a, faction_b, start_time, battles_fought,
                   faction_a_morale, faction_b_morale, faction_a_strength, faction_b_strength
            FROM faction_wars WHERE end_time IS NULL
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return results;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(ReadWarRow(stmt));
        }
        sqlite3_finalize(stmt);
        return results;
    }

    // =========================================================================
    // Timeline Cleanup & Seed Helpers
    // =========================================================================

    int PoliticalDB::CleanupFutureEvents(float currentGameTime) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return 0;

        int totalDeleted = 0;
        double gameTime = static_cast<double>(currentGameTime);

        // Delete future events
        {
            const char* sql = "DELETE FROM faction_events WHERE game_time > ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, gameTime);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    totalDeleted += sqlite3_changes(db_);
                }
                sqlite3_finalize(stmt);
            }
        }

        // Delete wars that started in the future
        {
            const char* sql = "DELETE FROM faction_wars WHERE start_time > ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, gameTime);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    totalDeleted += sqlite3_changes(db_);
                }
                sqlite3_finalize(stmt);
            }
        }

        // Reset last_event_time entries that are in the future
        {
            const char* sql = "UPDATE faction_relations SET last_event_time = NULL WHERE last_event_time > ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, gameTime);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        // Reset player standings modified in the future
        {
            const char* sql = "UPDATE player_faction_standing SET last_change_time = NULL WHERE last_change_time > ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, gameTime);
                sqlite3_step(stmt);
                sqlite3_finalize(stmt);
            }
        }

        if (totalDeleted > 0) {
            logger::info("PoliticalDB: Timeline cleanup deleted {} future rows (game_time > {:.2f})", totalDeleted, currentGameTime);
        }
        return totalDeleted;
    }

    bool PoliticalDB::SeedDefaultRelation(const std::string& factionA, const std::string& factionB, int score) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        auto [a, b] = OrderFactions(factionA, factionB);
        score = std::clamp(score, RELATION_MIN, RELATION_MAX);

        // INSERT OR IGNORE — only seeds if row doesn't already exist
        const char* sql = R"SQL(
            INSERT OR IGNORE INTO faction_relations (faction_a, faction_b, relation_score)
            VALUES (?, ?, ?)
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, score);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool PoliticalDB::ResetAllRelationScores() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        const char* sql = "UPDATE faction_relations SET relation_score = 0";
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            logger::error("PoliticalDB: Failed to reset relation scores: {}", errMsg ? errMsg : "unknown");
            sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

}  // namespace IntelEngine
