// RunPoliticalFeedCheck.py compiles actual persistence and getter implementations.
#include "FactionPolitics.h"
#include "FactionConfigLoader.h"
#include "FactionPairValidation.h"
#include "BattleManager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <random>
namespace IntelEngine {
class NPCIndex {
public:
    RE::Actor leader;
    static NPCIndex* GetSingleton() { static NPCIndex value; return &value; }
    RE::FormID FindFactionLeaderFormId(const std::string&) { return 1; }
    RE::Actor* GetActorFromFormId(RE::FormID) { return &leader; }
};
class PoliticalEngineBridge {
public:
    bool active = true;
    bool sidesExport = false;
    bool HasSidesExport() const { return sidesExport; }
    static PoliticalEngineBridge* GetSingleton() { static PoliticalEngineBridge value; return &value; }
    bool IsActive() const { return active; }
    nlohmann::json Context() const { return active ? nlohmann::json{{"territory", nlohmann::json::object()}} : nlohmann::json{}; }
};
FactionPolitics::PoliticsTickSnapshot VanillaSnapshot() {
    FactionPolitics::PoliticsTickSnapshot snap;
#include "PEVanillaSides.inc"
    return snap;
}
int FactionPolitics::DeclareWar(const std::string&, const std::string&, float) { throw std::runtime_error("unexpected war reaction"); }
bool FactionPolitics::EndWar(const std::string&, const std::string&, const std::string&, float) { throw std::runtime_error("unexpected surrender reaction"); }
int FactionPolitics::GetActiveWarId(const std::string&, const std::string&) { throw std::runtime_error("unexpected scheduled battle reaction"); }
std::string FactionPolitics::CheckEventManifestation(const std::string&, const std::string&, const std::string&) { throw std::runtime_error("unexpected manifested reaction"); }
int BattleManager::AddPendingBattle(const std::string&, const std::string&, const std::string&, const std::string&) { throw std::runtime_error("unexpected battle reaction"); }
int FactionPolitics::SyncVanillaFactionStandings() { return 0; } // No player actor in this offline fixture.
int FactionPolitics::CheckCrimeGoldStandings() { return 0; } // No engine bounties in this offline fixture.
nlohmann::json FactionPolitics::TestPEContext() {
    auto* db = PoliticalDB::GetSingleton();
    auto recentEvents = db->GetRecentEvents(15);
    nlohmann::json state;
    auto snap = VanillaSnapshot();
    auto factionsCopy = GetAllFactions();
#include "PEContext.inc"
    return state;
}
}
#include "PoliticalFeedMethods.inc"
#include "PhysicalPosition.inc"
using nlohmann::json;
static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static json CapturePosition(const std::vector<RE::Actor*>& a, const std::vector<RE::Actor*>& b) {
    auto* battle = IntelEngine::BattleManager::GetSingleton();
    battle->activeBattle_.reset();
    Require(battle->StartBattle("A", "B", "spawn", 0) > 0, "start isolated battle");
    for (auto* actor : a) Require(battle->RegisterActor(actor, "A", 0), "register side A");
    for (auto* actor : b) Require(battle->RegisterActor(actor, "B", 0), "register side B");
    return battle->activeBattle_->physicalPosition;
}
static void Sql(sqlite3* db, const char* sql) {
    Require(sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK, sqlite3_errmsg(db));
}
static void Config(const std::string& name) {
    std::filesystem::create_directories("Data/SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine");
    std::ofstream file("Data/SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/factions.yaml");
    file << "factions:\n  - id: A\n    name: " << name << "\n    hold: Whiterun\n"
            "    skyrim_faction_id: TestFaction\n    leader_names: [Leader]\n    allies: [B]\n    rivals: [C]\n"
            "  - id: B\n    name: Other\n    leader_names: []\n";
}
int main() try {
    using namespace IntelEngine;
    auto* db = PoliticalDB::GetSingleton();
    auto* politics = FactionPolitics::GetSingleton();
    Require(!db->GetPoliticalEventsAfter(0, 1)["ready"].get<bool>(), "uninitialized feed must gate");
    sqlite3* raw = nullptr;
    Require(sqlite3_open("./feed.db", &raw) == SQLITE_OK, "create legacy DB");
    Sql(raw, "CREATE TABLE faction_events(id INTEGER PRIMARY KEY AUTOINCREMENT,faction_a TEXT NOT NULL,"
             "faction_b TEXT,event_type TEXT NOT NULL,description TEXT NOT NULL,relation_delta INTEGER DEFAULT 0,"
             "game_time REAL,instigator_npc TEXT);"
             "INSERT INTO faction_events(faction_a,event_type,description,game_time) VALUES('A','insult','legacy',1);");
    sqlite3_close(raw);
    Require(db->Initialize("./feed.db", "same-save"), "upgrade legacy DB in place");
    const auto first = db->FeedIdentity();
    const auto uuid = first["db_uuid"];
    Require(first["save_id"] == "same-save" && uuid.get<std::string>().size() == 32 &&
            first["load_id"].get<std::string>().size() == 32 && !first["ready"].get<bool>(), "initial identity/readiness");
    Require(db->GetPoliticalEventsAfter(0, 20)["events"].empty(), "no data before cleanup");
    Require(db->CleanupFutureEvents(10.f) == 0, "clean legacy timeline");
    Require(db->GetPoliticalEventsAfter(0, 20)["events"][0]["description"] == "legacy", "migration preserves rows");
    // An explicit 3.7-style reader/writer must remain compatible after upgrade.
    Require(sqlite3_open("./feed.db", &raw) == SQLITE_OK, "open older reader");
    Sql(raw, "INSERT INTO faction_events(faction_a,faction_b,event_type,description,relation_delta,game_time,instigator_npc) "
             "VALUES('A',NULL,'brawl','old writer',0,2,'');");
    sqlite3_stmt* old = nullptr;
    Require(sqlite3_prepare_v2(raw, "SELECT id,faction_a,faction_b,event_type,description,relation_delta,game_time,instigator_npc "
            "FROM faction_events ORDER BY id", -1, &old, nullptr) == SQLITE_OK, "prepare 3.7 column list");
    Require(sqlite3_step(old) == SQLITE_ROW && std::string(reinterpret_cast<const char*>(sqlite3_column_text(old, 4))) == "legacy",
            "old reader preserves first row");
    Require(sqlite3_step(old) == SQLITE_ROW && std::string(reinterpret_cast<const char*>(sqlite3_column_text(old, 4))) == "old writer",
            "old reader reads its new row");
    Require(sqlite3_step(old) == SQLITE_DONE, "old reader sees exactly both rows");
    sqlite3_finalize(old);
    sqlite3_close(raw);
    const int pe = db->RecordEvent("A", "", "pe_territory", "Public PE news", 0, 3.f);
    Require(pe == 3, "PE fixture cursor");
    auto page = db->GetPoliticalEventsAfter(-1, 2);
    Require(page["events"][0]["id"] == 1 && page["events"][1]["id"] == 2 &&
            page["next_cursor"] == 2 && page["has_more"] == true, "ascending ID pagination");
    page = db->GetPoliticalEventsAfter(2, 2);
    Require(page["events"].size() == 1 && page["events"][0]["type"] == "pe_territory" &&
            page["events"][0]["position"].is_null() && page["next_cursor"] == pe && !page["has_more"].get<bool>(),
            "PE events retained, narrative position null, exact end");
    page = db->GetPoliticalEventsAfter(999, 1);
    Require(page["events"].empty() && page["next_cursor"] == 999 && !page["has_more"].get<bool>(), "empty cursor preserved");
    for (int i = 0; i < 205; ++i) Require(db->RecordEvent("A", "", "insult", "pagination", 0, 20.f) > pe, "insert future event");
    for (int limit : {0, -1}) {
        page = db->GetPoliticalEventsAfter(pe, limit);
        Require(page["events"].size() == 50 && page["next_cursor"] == pe + 50 && page["has_more"] == true,
                "nonpositive limit defaults to 50");
    }
    page = db->GetPoliticalEventsAfter(pe, std::numeric_limits<int>::max());
    Require(page["events"].size() == 200 && page["next_cursor"] == pe + 200 && page["has_more"] == true, "limit capped at 200");
    page = db->GetPoliticalEventsAfter(pe + 200, 5);
    Require(page["events"].size() == 5 && !page["has_more"].get<bool>(), "exact-limit terminal page");
    const int oldCursor = page["next_cursor"];
    const auto oldLoad = db->FeedIdentity()["load_id"];
    db->BeginLoad();
    Require(!db->FeedIdentity()["ready"].get<bool>() && db->FeedIdentity()["load_id"] != oldLoad, "revert gates and changes load identity");
    Require(db->CleanupFutureEvents(3.f) == 205, "rollback removes future rows");
    Require(db->GetPoliticalEventsAfter(oldCursor, 5)["events"].empty(), "old future cursor yields empty page");
    const int freshId = db->RecordEvent("A", "", "insult", "after rollback", 0, 3.f);
    Require(freshId > oldCursor && db->GetPoliticalEventsAfter(oldCursor, 1)["events"][0]["id"] == freshId,
            "AUTOINCREMENT never reuses deleted future IDs");
    // Full config reload and real serialization; modeled leader liveness is the engine seam.
    Config("Organization A");
    Require(politics->LoadConfig(), "load real faction config");
    politics->initialized_ = true;
    auto factions = json::parse(politics->GetFactionsJson());
    Require(factions["ready"] == true && factions["db_uuid"] == uuid && factions["load_id"] == db->FeedIdentity()["load_id"],
            "getters share identities");
    Require(factions["factions"][0]["disbanded"] == false && factions["factions"][0]["allies"] == json::array({"B"}) &&
            factions["factions"][0]["rivals"] == json::array({"C"}) && factions["factions"][0]["skyrim_faction_id"] == "TestFaction",
            "organization fields");
    auto revision = factions["revision"].get<unsigned long long>();
    Config("Renamed A");
    Require(politics->LoadConfig(), "reload changed config");
    factions = json::parse(politics->GetFactionsJson());
    Require(factions["revision"].get<unsigned long long>() > revision && factions["factions"][0]["name"] == "Renamed A", "config reload revision");
    revision = factions["revision"].get<unsigned long long>();
    NPCIndex::GetSingleton()->leader.dead = true;
    factions = json::parse(politics->GetFactionsJson());
    Require(factions["revision"].get<unsigned long long>() > revision && factions["factions"][0]["disbanded"] == true &&
            factions["factions"][0]["hold"] == "Whiterun", "disband retained and revision advanced");
    revision = factions["revision"].get<unsigned long long>();
    Require(json::parse(politics->GetFactionsJson())["revision"] == revision, "unchanged read stable revision");
    NPCIndex::GetSingleton()->leader.dead = false;
    Require(json::parse(politics->GetFactionsJson())["revision"].get<unsigned long long>() > revision, "undisband revision");
    RE::World world{0x3C};
    RE::Cell exterior{0x1234, false, &world}, interior{0x5678, true, nullptr};
    RE::Actor soldier;
    soldier.cell = &exterior; soldier.x = 120; soldier.y = -250; soldier.z = 32;
    auto position = CapturePosition({&soldier}, {});
    Require(position["worldspace"] == json({{"plugin", "Fixture.esm"}, {"local_id", 0x3C}}) &&
            position["cell"].is_null() && position["x"] == 120, "actual exterior spawn position");
    world.id = 0x0500003C;
    Require(CapturePosition({}, {&soldier}) == position, "late-arrival registration and load-order-independent identity");
    auto* battle = BattleManager::GetSingleton();
    soldier.x = 999;
    Require(battle->RegisterActor(&soldier, "A", 0) && battle->activeBattle_->physicalPosition == position,
            "reinforcement registration preserves occurrence position");
    soldier.x = 120;
    Require(db->StartWar("A", "B", 1.f) > 0, "start physical battle war");
    const auto beforeBattle = db->GetPoliticalEventsAfter(0, 200)["next_cursor"].get<int>();
    Require(politics->RecordOffScreenBattle("A", "B", "Real spawn", "draw", "Physical war battle", 0, 0, "", &position) > 0,
            "record physical active-war battle");
    page = db->GetPoliticalEventsAfter(beforeBattle, 10);
    Require(page["events"].size() == 1 && page["events"][0]["type"] == "battle_result" && page["events"][0]["delta"] == -10 &&
            page["events"][0]["position"] == position && page["events"][0]["location_name"] == "Real spawn", "war battle position/delta round trip");
    politics->RecalculateRelationScores();
    Require(db->GetRelation("A", "B") == -10, "physical battle replay applies delta once");
    soldier.cell = &interior;
    position = CapturePosition({&soldier}, {});
    Require(position["worldspace"].is_null() && position["cell"] == json({{"plugin", "Fixture.esm"}, {"local_id", 0x5678}}),
            "interior cell identity");
    const int noWar = politics->RecordPoliticalEvent("A", "", "battle_result", "Physical no-war battle", 0, 3.f, "Interior spawn", position);
    Require(db->GetPoliticalEventsAfter(noWar - 1, 1)["events"][0]["position"] == position, "no-war position round trip");
    soldier.x = std::numeric_limits<float>::infinity();
    Require(CapturePosition({&soldier}, {}).is_null(), "nonfinite position unknown");
    soldier.x = 0; soldier.cell = nullptr;
    Require(CapturePosition({&soldier}, {}).is_null(), "missing cell position unknown");
    Require(battle->RegisterActor(&soldier, "A", 0) && battle->activeBattle_->physicalPosition.is_null(), "unknown late actor stays null");
    soldier.cell = &exterior;
    Require(battle->RegisterActor(&soldier, "A", 0) && !battle->activeBattle_->physicalPosition.is_null(),
            "first later reliable registration fills unknown position");
    world.file = nullptr;
    Require(CapturePosition({&soldier}, {}).is_null(), "unowned worldspace unknown");
    world.file = &RE::fixtureFile; world.id = 0xFF00003C;
    Require(CapturePosition({&soldier}, {}).is_null(), "dynamic worldspace unknown");
    world.id = 0x0500003C;
    soldier.cell = &exterior; exterior.world = nullptr;
    Require(CapturePosition({&soldier}, {}).is_null(), "missing exterior worldspace unknown");
    const json unknown = nullptr;
    Require(politics->RecordOffScreenBattle("A", "B", "Unknown", "draw", "Unknown physical position", 0, 0, "", &unknown) > 0,
            "unknown physical battle still records");
    const auto examples = db->GetPoliticalEventsAfter(noWar - 1, 10);
    Require(examples["events"][1]["position"].is_null() && examples["events"][1]["type"] == "battle_result", "unknown physical location explicit null");
    std::cout << "EVENTS_EXAMPLE=" << examples.dump() << '\n';
    std::cout << "FACTIONS_EXAMPLE=" << politics->GetFactionsJson() << '\n';
    Require(politics->ValidateEvent("", "", "pe_territory", 0), "faction-less PE news allowed");
    Require(!politics->ValidateEvent("", "", "insult", 0) &&
            !politics->ValidateEvent("missing", "", "pe_ruler", 0) &&
            !politics->ValidateEvent("", "missing", "pe_ruler", 0) &&
            !politics->ValidateEvent("A", "A", "pe_campaign", 0) &&
            !politics->ValidateEvent("", "B", "pe_gift", 5), "PE exception preserves invalid-pair and zero-delta boundaries");
    auto* keyword=RE::TESForm::LookupByID<RE::BGSKeyword>(0x2A456);
    RE::BGSKeyword wrong{};
    RE::BGSLocation empire{{1,1,&RE::fixtureFile},"Imperial hold",{{&wrong,2.f},{keyword,1.f}}};
    RE::BGSLocation storm{{2,2,&RE::fixtureFile},"Stormcloak hold",{{keyword,2.f}}};
    RE::BGSLocation invalid{{3,3,&RE::fixtureFile},"Unknown hold",{{keyword,1.5f}}};
    auto* forms=RE::TESDataHandler::GetSingleton(); forms->locations={nullptr,&empire,&storm,&invalid};
    Require(VanillaSnapshot().vanillaSides==json::array({{{"location_id",1},{"name","Imperial hold"},{"side_id",1}},
            {{"location_id",2},{"name","Stormcloak hold"},{"side_id",2}}}), "fallback reads actual CWOwner values, not keyword presence");
    politics->factions_[0].skyrimFactionId="CWImperialFaction"; politics->factions_[1].skyrimFactionId="CWSonsFaction";
    auto fallback=politics->TestPEContext()["political_engine"];
    Require(fallback["vanilla_civil_war"]==VanillaSnapshot().vanillaSides &&
            fallback["civil_war_organizations"]==json{{"1","A"},{"2","B"}}, "fallback is vanilla-only and organizations match engine faction IDs");
    PoliticalEngineBridge::GetSingleton()->sidesExport=true; const int reads=forms->reads;
    Require(!politics->TestPEContext()["political_engine"].contains("vanilla_civil_war") &&
            forms->reads==reads, "present optional export forbids CWOwner fallback reads and context");
    PoliticalEngineBridge::GetSingleton()->sidesExport=false;
    const int relationBeforePE = db->GetRelation("A", "B");
    const int publicNews = politics->RecordPoliticalEvent("", "", "pe_territory", "Fellburg took Southern Reach from Markarth.", 0, 3.f);
    auto publicRow = db->GetPoliticalEventsAfter(publicNews-1, 1)["events"][0];
    Require(publicNews > 0 && publicRow["type"] == "pe_territory" && publicRow["faction_a"] == "" &&
            publicRow["delta"] == 0 && publicRow["description"] == "Fellburg took Southern Reach from Markarth." &&
            db->GetRelation("A", "B") == relationBeforePE, "public factual PE event persists without relation changes");
    auto context = politics->TestPEContext();
    Require(context["political_engine"]["react_to"].contains(std::to_string(publicNews)), "new PE event eligible next tick");
    politics->peReactionsMoveRelations_ = false;
    json reaction = {{"should_act",true},{"faction_a","A"},{"faction_b","B"},{"event_type","political_reaction"},
        {"description","Organization leaders criticized the public capture."},{"relation_delta",5},{"reacts_to",publicNews},
        {"player_standing_changes",json::array({{{"faction","A"},{"delta",5}}})}};
    auto reacted = json::parse(politics->ProcessPoliticalDMResponse(reaction.dump(),1));
    Require(reacted["acted"] == true && reacted["reacts_to"] == publicNews &&
            db->GetRelation("A","B") == relationBeforePE && db->GetPlayerStanding("A") == 0,
            "toggle off records reaction without relation or player-standing changes");
    const int reactionEvent = reacted["eventId"];
    Require(db->GetPoliticalEventsAfter(reactionEvent-1,1)["events"][0]["delta"] == 0, "recorded reaction delta zero");
    Require(json::parse(politics->ProcessPoliticalDMResponse(reaction.dump(),1))["acted"] == false &&
            db->GetPoliticalEventsAfter(reactionEvent,1)["events"] == json::array(), "one reaction per development");
    const int expires = politics->RecordPoliticalEvent("","","pe_ruler","A public ruler appointment.",0,3.f);
    politics->TestPEContext(); politics->TestPEContext(); reaction["reacts_to"]=expires;
    Require(json::parse(politics->ProcessPoliticalDMResponse(reaction.dump(),1))["acted"] == false, "eligibility expires after next tick");
    const int fresh = politics->RecordPoliticalEvent("","","pe_visit","A public visit.",0,3.f);
    politics->TestPEContext(); reaction["reacts_to"]=fresh; politics->peReactionsMoveRelations_=true;
    reacted=json::parse(politics->ProcessPoliticalDMResponse(reaction.dump(),1));
    Require(reacted["acted"]==true && db->GetRelation("A","B")==relationBeforePE+5 &&
            db->GetPlayerStanding("A")==4, "toggle on applies deltas; normal one-point standing decay remains");
    politics->peReactionsMoveRelations_=false; reaction.erase("reacts_to"); reaction["event_type"]="trade_deal";
    reacted=json::parse(politics->ProcessPoliticalDMResponse(reaction.dump(),1));
    Require(reacted["acted"]==true && db->GetRelation("A","B")==relationBeforePE+10 &&
            db->GetPlayerStanding("A")==8, "toggle does not suppress IE's own untagged politics");
    { auto s=reaction; s["event_type"]="surrender"; s["faction_b"]="C"; s["description"]="A surrenders without a war.";
      Require(json::parse(politics->ProcessPoliticalDMResponse(s.dump(),1))["error"]=="surrender requires an active war", "surrender without active war rejected before EndWar"); }
    PoliticalEngineBridge::GetSingleton()->active=false;
    Require(!politics->TestPEContext().contains("political_engine") && politics->eligiblePE_.empty(), "inactive PE omits context and clears eligibility");
    PoliticalEngineBridge::GetSingleton()->active=true;
    const int oldLoadEvent=politics->RecordPoliticalEvent("","","pe_visit","A pre-load visit.",0,3.f);
    politics->TestPEContext(); reaction["event_type"]="political_reaction"; reaction["reacts_to"]=oldLoadEvent;
    const auto priorLoad = db->FeedIdentity()["load_id"];
    db->Shutdown();
    Require(db->Initialize("./feed.db", "same-save") && db->FeedIdentity()["db_uuid"] == uuid &&
            db->FeedIdentity()["load_id"] != priorLoad && !db->FeedIdentity()["ready"].get<bool>(), "same-save reload changes load only");
    Require(!json::parse(politics->GetFactionsJson())["ready"].get<bool>(), "factions gate with DB");
    Require(db->CleanupFutureEvents(3.f) >= 0, "reload cleanup");
    Require(json::parse(politics->GetFactionsJson())["revision"] == 0, "faction revision resets per load");
    Require(json::parse(politics->ProcessPoliticalDMResponse(reaction.dump(),1))["acted"]==false, "load identity rejects previously eligible reaction");
    Require(politics->TestPEContext()["political_engine"]["react_to"].empty(), "load clears pending and eligible reactions");
    Require(sqlite3_open("./feed.db", &raw) == SQLITE_OK, "open failure fixture");
    Sql(raw, "CREATE TRIGGER fail_cleanup BEFORE DELETE ON faction_events BEGIN SELECT RAISE(ABORT,'injected cleanup failure'); END;");
    const int future = db->RecordEvent("A", "", "insult", "must survive failed transaction", 0, 99.f);
    const int warnings = logger::warnings;
    db->BeginLoad();
    Require(db->CleanupFutureEvents(3.f) == -1 && !db->FeedIdentity()["ready"].get<bool>() && logger::warnings > warnings,
            "cleanup failure warns and keeps readiness false");
    Sql(raw, "DROP TRIGGER fail_cleanup;");
    sqlite3_close(raw);
    Require(db->CleanupFutureEvents(100.f) == 0 && !db->FeedIdentity()["ready"].get<bool>(), "failure latched for load");
    db->BeginLoad();
    Require(db->CleanupFutureEvents(100.f) == 0 && db->GetPoliticalEventsAfter(future - 1, 1)["events"][0]["id"] == future,
            "failed cleanup rolled back and new load recovers");
    db->Shutdown();
    std::filesystem::remove("./feed.db");
    Require(db->Initialize("./feed.db", "same-save") && db->FeedIdentity()["db_uuid"] != uuid &&
            db->FeedIdentity()["save_id"] == "same-save", "DB recreation changes generation with same save ID");
    db->Shutdown();
    std::cout << "PASS: feed pagination/limits, rollback cursors, identities/readiness/failure latch, legacy upgrade/3.7 compatibility, "
                 "PE retention/null narratives and next-tick reaction toggle/dedupe/expiry/load isolation, faction revisions/disband, actual spawn positions and both battle recording paths\n";
} catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
}
