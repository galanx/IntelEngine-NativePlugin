// Run with RunBattleRelationPersistenceCheck.py from a VS developer shell.
// Production relation/war/event code and SQLite; only game clock/log/output are stubbed.
#include "FactionPolitics.h"
#include "FactionPoliticsMethods.inc"
#include <sqlite3.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() try {
    using namespace IntelEngine;
    auto* db = PoliticalDB::GetSingleton();
    auto* politics = FactionPolitics::GetSingleton();
    Require(db->Initialize("./relations.db"), "open isolated DB");
    politics->initialized_ = true;
    politics->defaultRelations_ = {{"A", "B", -40}, {"C", "D", -60}};
    politics->RecalculateRelationScores();
    Require(db->StartWar("B", "A", 1.f) >= 0, "start battle war");
    Require(db->StartWar("C", "D", 1.f) >= 0, "start peace war");
    Require(db->RecordEvent("B", "A", "battle_result", "Existing battle narrative", -5, 1.f) >= 0,
            "record existing visible event");
    db->AdjustRelation("B", "A", -5);
    Require(politics->RecordOffScreenBattle("B", "A", "Whiterun", "draw", "Battle narrative", 5, 7, "") >= 0,
            "resolve battle with reversed faction order");
    Require(politics->EndWar("D", "C", "C", 3.f), "end independent peace war");
    const int battle = db->GetRelation("A", "B");
    const int peace = db->GetRelation("C", "D");
    // peace: +10, and at least up to the truce score (-40), so the pair is not Critical after the war
    Require(battle == -55 && peace == -40, "battle -10 and peace (truce) applied live");
    db->Shutdown();
    Require(db->Initialize("./relations.db"), "reopen persisted DB");
    db->CleanupFutureEvents(3.f);
    politics->RecalculateRelationScores();
    const bool battlePreserved = db->GetRelation("A", "B") == battle;
    const bool peacePreserved = db->GetRelation("C", "D") == peace;
    std::cout << "battle: " << battle << " -> " << db->GetRelation("A", "B")
              << "; peace: " << peace << " -> " << db->GetRelation("C", "D") << '\n';
    Require(battlePreserved && peacePreserved, "load-time rebuild lost battle or peace relation delta");
    Require(!politics->EndWar("D", "C", "C", 4.f), "ended war must not award peace twice");
    Require(politics->RecordOffScreenBattle("D", "C", "Whiterun", "draw", "", 0, 0, "") == -1,
            "battle without active war must reject");
    Require(db->GetRelation("C", "D") == peace, "rejected operations must not change relations");
    const auto recent = db->GetRecentEvents(1);
    Require(recent.size() == 1 && recent.front().description == "Existing battle narrative",
            "replay-only deltas must not duplicate or displace visible narratives");
    db->CleanupFutureEvents(1.5f);
    politics->RecalculateRelationScores();
    Require(db->GetRelation("A", "B") == -45 && db->GetRelation("C", "D") == -60,
            "loading earlier save must remove future battle and peace deltas");
    Require(db->SetRelation("A", "A", 99) == false, "self relation must not be set");
    Require(db->AdjustRelation("A", "A", 99) == 0, "self relation must not be adjusted");
    Require(db->SeedDefaultRelation("A", "A", 99) == false, "self relation must not be seeded");
    Require(db->RecordEvent("A", "A", "insult", "self event", -5, 2.f) == -1,
            "self political event must not be recorded");
    Require(db->StartWar("A", "A", 2.f) == -1, "self war must not start");
    Require(db->RecordEvent("A", "", "player_combat", "one-sided event", 0, 2.f) >= 0,
            "one-sided political event must remain valid");
    db->Shutdown();
    sqlite3* raw = nullptr;
    Require(sqlite3_open("./relations.db", &raw) == SQLITE_OK, "open fixture DB for legacy self relation");
    Require(sqlite3_exec(raw,
        "INSERT INTO faction_relations (faction_a, faction_b, relation_score) VALUES ('A', 'A', 0)",
        nullptr, nullptr, nullptr) == SQLITE_OK, "insert legacy self relation fixture");
    Require(sqlite3_exec(raw,
        "INSERT INTO faction_events (faction_a,event_type,description,game_time) VALUES "
        "('A','insult','undated',NULL),('A','insult','dated-zero',0);"
        "INSERT INTO war_battles (war_id,location_name,attacker,defender,game_time) VALUES "
        "(777,'undated','A','B',NULL),(777,'dated-zero','A','B',0);",
        nullptr, nullptr, nullptr) == SQLITE_OK, "insert missing and zero timestamp fixtures");
    sqlite3_close(raw);
    Require(db->Initialize("./relations.db"), "reopen DB with legacy self relation");
    const auto relations = db->GetAllRelations();
    Require(std::none_of(relations.begin(), relations.end(),
        [](const FactionRelation& relation) { return relation.factionA == relation.factionB; }),
        "legacy self relation must be hidden from relation reads");
    Require(db->GetRelation("A", "A") == 0, "legacy self relation must not be returned");
    const auto events = db->GetRecentEvents(100);
    for (const auto& description : {"undated", "dated-zero"}) {
        const auto event = std::find_if(events.begin(), events.end(),
            [&](const FactionEvent& row) { return row.description == description; });
        Require(event != events.end(), "timestamp event fixture missing");
        Require(event->hasGameTime == (event->description == "dated-zero") && event->gameTime == 0.f,
                "event read must distinguish missing timestamp from genuine zero");
    }
    const auto battles = db->GetBattlesForWar(777, 10);
    Require(battles.size() == 2, "timestamp battle fixtures missing");
    for (const auto& battle : battles) {
        Require(battle.hasGameTime == (battle.locationName == "dated-zero") && battle.gameTime == 0.f,
                "battle read must distinguish missing timestamp from genuine zero");
    }
    db->Shutdown();
    std::cout << "PASS: battle/peace persistence, rejected self pairs, legacy filtering and missing/zero timestamps\n";
}
catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
