// Run with RunBattleRelationPersistenceCheck.py from a VS developer shell.
// Production relation/war/event code and SQLite; only game clock/log/output are stubbed.
#include "FactionPolitics.h"
#include "FactionPoliticsMethods.inc"
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
    Require(battle == -55 && peace == -50, "battle -10 and peace +10 applied live");
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
    db->Shutdown();
    std::cout << "PASS: battle and peace survive reopen/rebuild, hidden bookkeeping, rejected repeats and timeline rollback\n";
}
catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
