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

            CREATE TABLE IF NOT EXISTS player_standing_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                faction_id TEXT NOT NULL,
                delta INTEGER NOT NULL,
                reason TEXT,
                game_time REAL NOT NULL
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

            CREATE TABLE IF NOT EXISTS war_battles (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                war_id INTEGER NOT NULL,
                location_name TEXT NOT NULL,
                game_time REAL,
                attacker TEXT NOT NULL,
                defender TEXT NOT NULL,
                result TEXT,
                attacker_losses INTEGER DEFAULT 0,
                defender_losses INTEGER DEFAULT 0,
                player_participated BOOLEAN DEFAULT 0,
                player_side TEXT,
                narrative TEXT,
                FOREIGN KEY (war_id) REFERENCES faction_wars(id)
            );

            CREATE INDEX IF NOT EXISTS idx_events_game_time ON faction_events(game_time DESC);
            CREATE INDEX IF NOT EXISTS idx_events_faction ON faction_events(faction_a, faction_b);
            CREATE INDEX IF NOT EXISTS idx_wars_active ON faction_wars(end_time) WHERE end_time IS NULL;
            CREATE INDEX IF NOT EXISTS idx_battles_war ON war_battles(war_id);
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
        logger::info("PoliticalDB: AdjustPlayerStanding('{}') {} + {} = {} (clamped: {})",
            factionId, current, delta, current + delta, newStanding);

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

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return current;
        }
        sqlite3_finalize(stmt);

        // Record the delta in history for save-load recalculation
        {
            const char* histSql = "INSERT INTO player_standing_history (faction_id, delta, reason, game_time) VALUES (?, ?, '', ?)";
            sqlite3_stmt* histStmt = nullptr;
            if (sqlite3_prepare_v2(db_, histSql, -1, &histStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(histStmt, 1, factionId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(histStmt, 2, delta);
                sqlite3_bind_double(histStmt, 3, gameTime);
                sqlite3_step(histStmt);
                sqlite3_finalize(histStmt);
            }
        }

        return newStanding;
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

    void PoliticalDB::RecalculatePlayerStandings() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return;

        sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);

        // Migration: if history table is empty but standings exist, seed history from current standings.
        // This preserves pre-existing standings from saves created before the history system.
        {
            int historyCount = 0;
            sqlite3_stmt* countStmt = nullptr;
            if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM player_standing_history", -1, &countStmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(countStmt) == SQLITE_ROW) historyCount = sqlite3_column_int(countStmt, 0);
                sqlite3_finalize(countStmt);
            }

            if (historyCount == 0) {
                // Seed history from existing standings (one entry per faction with current standing as delta)
                sqlite3_stmt* readStmt = nullptr;
                if (sqlite3_prepare_v2(db_, "SELECT faction_id, standing, last_change_time FROM player_faction_standing WHERE standing != 0",
                        -1, &readStmt, nullptr) == SQLITE_OK) {
                    int seeded = 0;
                    while (sqlite3_step(readStmt) == SQLITE_ROW) {
                        std::string fid = SafeColumnText(readStmt, 0);
                        int standing = sqlite3_column_int(readStmt, 1);
                        double changeTime = sqlite3_column_double(readStmt, 2);
                        if (changeTime <= 0.0) changeTime = 1.0;  // fallback for missing timestamps

                        sqlite3_stmt* insertStmt = nullptr;
                        if (sqlite3_prepare_v2(db_, "INSERT INTO player_standing_history (faction_id, delta, reason, game_time) VALUES (?, ?, 'migrated', ?)",
                                -1, &insertStmt, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(insertStmt, 1, fid.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(insertStmt, 2, standing);
                            sqlite3_bind_double(insertStmt, 3, changeTime);
                            sqlite3_step(insertStmt);
                            sqlite3_finalize(insertStmt);
                            seeded++;
                        }
                    }
                    sqlite3_finalize(readStmt);
                    if (seeded > 0) {
                        logger::info("PoliticalDB: Migrated {} existing standings to history table", seeded);
                    }
                }
            }
        }

        // Reset all player standings to 0
        sqlite3_exec(db_, "DELETE FROM player_faction_standing", nullptr, nullptr, nullptr);

        // Replay all standing history events (chronological) to rebuild accurate standings
        const char* sql = "SELECT faction_id, delta FROM player_standing_history ORDER BY game_time ASC";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

        std::unordered_map<std::string, int> standings;
        int replayed = 0;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string factionId = SafeColumnText(stmt, 0);
            int delta = sqlite3_column_int(stmt, 1);
            standings[factionId] = std::clamp(standings[factionId] + delta, RELATION_MIN, RELATION_MAX);
            replayed++;
        }
        sqlite3_finalize(stmt);

        // Write recalculated standings back
        for (const auto& [factionId, standing] : standings) {
            const char* upsert = R"SQL(
                INSERT INTO player_faction_standing (faction_id, standing) VALUES (?, ?)
                ON CONFLICT(faction_id) DO UPDATE SET standing = excluded.standing
            )SQL";
            sqlite3_stmt* uStmt = nullptr;
            if (sqlite3_prepare_v2(db_, upsert, -1, &uStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(uStmt, 1, factionId.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(uStmt, 2, standing);
                sqlite3_step(uStmt);
                sqlite3_finalize(uStmt);
            }
        }

        sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
        logger::info("PoliticalDB: Recalculated player standings from {} history events, {} factions", replayed, standings.size());
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

        sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr);

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

        // Delete battles from future wars (before deleting the wars themselves)
        {
            const char* sql = "DELETE FROM war_battles WHERE war_id IN (SELECT id FROM faction_wars WHERE start_time > ?)";
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

        // Reopen wars that ended in the future (they were still active at the save point)
        {
            const char* sql = "UPDATE faction_wars SET end_time = NULL, victor = NULL WHERE end_time > ? AND start_time <= ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, gameTime);
                sqlite3_bind_double(stmt, 2, gameTime);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    int reopened = sqlite3_changes(db_);
                    if (reopened > 0) {
                        logger::info("PoliticalDB: Reopened {} wars that ended in the future", reopened);
                    }
                }
                sqlite3_finalize(stmt);
            }
        }

        // Delete future battles within surviving wars
        {
            const char* sql = "DELETE FROM war_battles WHERE game_time > ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, gameTime);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    int cleaned = sqlite3_changes(db_);
                    if (cleaned > 0) {
                        totalDeleted += cleaned;
                        logger::info("PoliticalDB: Cleaned {} future battles from surviving wars", cleaned);
                    }
                }
                sqlite3_finalize(stmt);
            }
        }

        // Delete future player standing history entries
        {
            const char* sql = "DELETE FROM player_standing_history WHERE game_time > ?";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_double(stmt, 1, gameTime);
                if (sqlite3_step(stmt) == SQLITE_DONE) {
                    int cleaned = sqlite3_changes(db_);
                    if (cleaned > 0) {
                        totalDeleted += cleaned;
                        logger::info("PoliticalDB: Cleaned {} future player standing entries", cleaned);
                    }
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
        sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
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

    // =========================================================================
    // War Lifecycle
    // =========================================================================

    int PoliticalDB::StartWar(const std::string& factionA, const std::string& factionB,
                               float startTime, int strengthA, int strengthB) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return -1;

        auto [a, b] = OrderFactions(factionA, factionB);

        // If OrderFactions swapped the pair, swap strengths to match
        if (a != factionA) {
            std::swap(strengthA, strengthB);
        }

        // Check no active war already exists between these factions
        {
            const char* checkSql = "SELECT id FROM faction_wars WHERE faction_a = ? AND faction_b = ? AND end_time IS NULL";
            sqlite3_stmt* checkStmt = nullptr;
            if (sqlite3_prepare_v2(db_, checkSql, -1, &checkStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(checkStmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(checkStmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);
                bool exists = sqlite3_step(checkStmt) == SQLITE_ROW;
                sqlite3_finalize(checkStmt);
                if (exists) {
                    logger::warn("PoliticalDB: War already active between {} and {}", a, b);
                    return -1;
                }
            }
        }

        const char* sql = R"SQL(
            INSERT INTO faction_wars (faction_a, faction_b, start_time, faction_a_morale, faction_b_morale,
                                      faction_a_strength, faction_b_strength)
            VALUES (?, ?, ?, 100, 100, ?, ?)
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

        sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, startTime);
        sqlite3_bind_int(stmt, 4, strengthA);
        sqlite3_bind_int(stmt, 5, strengthB);

        int warId = -1;
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            warId = static_cast<int>(sqlite3_last_insert_rowid(db_));
        }
        sqlite3_finalize(stmt);

        // Update war_active flag on the relation row
        if (warId >= 0) {
            const char* updateSql = "UPDATE faction_relations SET war_active = 1 WHERE faction_a = ? AND faction_b = ?";
            sqlite3_stmt* updateStmt = nullptr;
            if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(updateStmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(updateStmt);
                sqlite3_finalize(updateStmt);
            }
        }

        logger::info("PoliticalDB: War #{} started between {} and {} (strength {}/{})",
                     warId, a, b, strengthA, strengthB);
        return warId;
    }

    bool PoliticalDB::UpdateWarState(int warId, int moraleA, int moraleB,
                                      int strengthA, int strengthB, int battlesFought) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        const char* sql = R"SQL(
            UPDATE faction_wars SET
                faction_a_morale = ?, faction_b_morale = ?,
                faction_a_strength = ?, faction_b_strength = ?,
                battles_fought = ?
            WHERE id = ? AND end_time IS NULL
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_int(stmt, 1, std::clamp(moraleA, 0, 100));
        sqlite3_bind_int(stmt, 2, std::clamp(moraleB, 0, 100));
        sqlite3_bind_int(stmt, 3, std::clamp(strengthA, 0, 100));
        sqlite3_bind_int(stmt, 4, std::clamp(strengthB, 0, 100));
        sqlite3_bind_int(stmt, 5, battlesFought);
        sqlite3_bind_int(stmt, 6, warId);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool PoliticalDB::EndWar(int warId, const std::string& victor, float endTime) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        // Get faction pair before ending (for war_active flag update)
        std::string factionA, factionB;
        {
            const char* getSql = "SELECT faction_a, faction_b FROM faction_wars WHERE id = ?";
            sqlite3_stmt* getStmt = nullptr;
            if (sqlite3_prepare_v2(db_, getSql, -1, &getStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(getStmt, 1, warId);
                if (sqlite3_step(getStmt) == SQLITE_ROW) {
                    factionA = SafeColumnText(getStmt, 0);
                    factionB = SafeColumnText(getStmt, 1);
                }
                sqlite3_finalize(getStmt);
            }
        }

        const char* sql = "UPDATE faction_wars SET end_time = ?, victor = ? WHERE id = ? AND end_time IS NULL";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

        sqlite3_bind_double(stmt, 1, endTime);
        sqlite3_bind_text(stmt, 2, victor.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, warId);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
        sqlite3_finalize(stmt);

        // Clear war_active flag on the relation row
        if (ok && !factionA.empty()) {
            const char* updateSql = "UPDATE faction_relations SET war_active = 0 WHERE faction_a = ? AND faction_b = ?";
            sqlite3_stmt* updateStmt = nullptr;
            if (sqlite3_prepare_v2(db_, updateSql, -1, &updateStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(updateStmt, 1, factionA.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(updateStmt, 2, factionB.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(updateStmt);
                sqlite3_finalize(updateStmt);
            }
        }

        logger::info("PoliticalDB: War #{} ended, victor: {}", warId, victor);
        return ok;
    }

    int PoliticalDB::RecordBattle(int warId, const std::string& locationName, float gameTime,
                                   const std::string& attacker, const std::string& defender,
                                   const std::string& result, int attackerLosses, int defenderLosses,
                                   const std::string& narrative) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return -1;

        std::string safeNarrative = narrative;
        if (safeNarrative.size() > MAX_EVENT_DESCRIPTION_LENGTH) {
            safeNarrative.resize(MAX_EVENT_DESCRIPTION_LENGTH);
            safeNarrative += "...";
        }

        const char* sql = R"SQL(
            INSERT INTO war_battles (war_id, location_name, game_time, attacker, defender,
                                     result, attacker_losses, defender_losses, narrative)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return -1;

        sqlite3_bind_int(stmt, 1, warId);
        sqlite3_bind_text(stmt, 2, locationName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 3, gameTime);
        sqlite3_bind_text(stmt, 4, attacker.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, defender.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, result.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 7, attackerLosses);
        sqlite3_bind_int(stmt, 8, defenderLosses);
        sqlite3_bind_text(stmt, 9, safeNarrative.c_str(), -1, SQLITE_TRANSIENT);

        int battleId = -1;
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            battleId = static_cast<int>(sqlite3_last_insert_rowid(db_));
        }
        sqlite3_finalize(stmt);

        return battleId;
    }

    std::vector<BattleRow> PoliticalDB::GetBattlesForWar(int warId, int maxCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<BattleRow> battles;
        if (!db_) return battles;

        const char* sql = R"SQL(
            SELECT id, war_id, location_name, game_time, attacker, defender,
                   result, attacker_losses, defender_losses, narrative
            FROM war_battles WHERE war_id = ?
            ORDER BY game_time DESC LIMIT ?
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return battles;

        sqlite3_bind_int(stmt, 1, warId);
        sqlite3_bind_int(stmt, 2, maxCount);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            BattleRow b;
            b.id = sqlite3_column_int(stmt, 0);
            b.warId = sqlite3_column_int(stmt, 1);
            b.locationName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            b.gameTime = static_cast<float>(sqlite3_column_double(stmt, 3));
            b.attacker = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            b.defender = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (sqlite3_column_text(stmt, 6)) b.result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            b.attackerLosses = sqlite3_column_int(stmt, 7);
            b.defenderLosses = sqlite3_column_int(stmt, 8);
            if (sqlite3_column_text(stmt, 9)) b.narrative = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
            battles.push_back(b);
        }
        sqlite3_finalize(stmt);
        return battles;
    }

    std::optional<FactionWar> PoliticalDB::GetMostRecentWar(const std::string& factionA, const std::string& factionB) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return std::nullopt;

        auto [a, b] = OrderFactions(factionA, factionB);

        // Selects end_time (col 9) and victor (col 10) in addition to standard war columns
        const char* sql = R"SQL(
            SELECT id, faction_a, faction_b, start_time, battles_fought,
                   faction_a_morale, faction_b_morale, faction_a_strength, faction_b_strength,
                   end_time, victor
            FROM faction_wars WHERE faction_a = ? AND faction_b = ?
            ORDER BY start_time DESC LIMIT 1
        )SQL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

        sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_TRANSIENT);

        std::optional<FactionWar> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            auto w = ReadWarRow(stmt);
            // ReadWarRow only reads cols 0-8; manually read end_time and victor
            w.endTime = static_cast<float>(sqlite3_column_double(stmt, 9));
            w.victor = SafeColumnText(stmt, 10);
            result = w;
        }
        sqlite3_finalize(stmt);
        return result;
    }

}  // namespace IntelEngine
