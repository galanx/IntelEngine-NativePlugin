"""Offline production bridge check; samples remain outside this repository.
Usage (VS2022 shell): python SKSE/tests/RunPoliticalEngineBridgeCheck.py <triplet> <sample-dir>
Only DLL lookup, SKSE event types and async scheduling are modeled.
"""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / "src"
deps, samples = map(pathlib.Path, sys.argv[1:3])
for name, expected in json.loads((samples / "MANIFEST.json").read_text()).items():
    assert hashlib.sha256((samples / name).read_bytes()).hexdigest() == expected, name
print("PASS all private sample MANIFEST hashes", flush=True)
with tempfile.TemporaryDirectory(prefix="pe-bridge-") as directory:
    work = pathlib.Path(directory)
    header = (src / "PoliticalEngineBridge.h").read_text().replace('#include "Plugin.h"', '#include "FakePlugin.h"')
    (work / "PoliticalEngineBridge.h").write_text(header)
    (work / "FakePlugin.h").write_text(r'''
#pragma once
#include <string>
namespace RE {
enum class BSEventNotifyControl { kContinue };
template<class T> struct BSTEventSource {};
template<class T> struct BSTEventSink { virtual BSEventNotifyControl ProcessEvent(const T*, BSTEventSource<T>*) = 0; };
}
namespace SKSE {
struct ModCallbackEvent { std::string eventName, strArg; };
struct Source { template<class T> void AddEventSink(T*) {} };
inline Source* GetModCallbackEventSource() { return nullptr; }
}
namespace logger { template<class... T> void info(T&&...) {} template<class... T> void debug(T&&...) {} }
namespace spdlog { namespace level { enum level_enum { debug }; } struct L { bool should_log(level::level_enum) { return true; } }; inline L* default_logger() { static L l; return &l; } }
namespace IntelEngine::AsyncDispatch { template<class F> void Submit(F f) { f(); } }
''')
    source = (src / "PoliticalEngineBridge.cpp").read_text()
    source = source[source.index("namespace IntelEngine {"):]
    code = r'''
#include "PoliticalEngineBridge.h"
#include "PoliticalEngineReadJSON.h"
#include "PoliticalEngineChanges.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
using nlohmann::json;
static json state;
static unsigned api = 3;
static bool installed = true;
static bool journalExport = true;
static bool sidesExport = true;
// Live PE 0.8.0 RC2 shape (2026-11-10): fields at top level, no "political_sides080" wrapper.
static json sides = {{"territory_rule","current_owner_realm"},{"player_side_id",1},{"realms",json::array({{{"realm_id",3},{"side_id",1}},{{"realm_id",9},{"side_id",2}}})}};
static unsigned ReadSides(char* out,unsigned capacity) {
    auto text=sides.dump(); auto size=static_cast<unsigned>(text.size()+1);
    if(out && capacity>=size) std::memcpy(out,text.c_str(),size);
    return size;
}
static json journal = {{"intel_initiatives080",{{"events",json::array({{{"id","old"},{"event_type","pe_visit"},{"source",20},{"target",104641},{"relation_delta",0},{"text","A prior public visit."}}})}}}};
static unsigned ReadJournal(char* out,unsigned capacity) {
    auto text=journal.dump(); auto size=static_cast<unsigned>(text.size()+1);
    if(out && capacity>=size) std::memcpy(out,text.c_str(),size);
    return size;
}
static IntelEngine::PoliticalEngineBridge* resetDuringRead = nullptr;
static unsigned Version() { return api; }
static unsigned Read(char* out, unsigned capacity) {
    if (resetDuringRead) { resetDuringRead->MarkDirty(true); resetDuringRead = nullptr; }
    auto text = state.dump(); auto size = static_cast<unsigned>(text.size()+1);
    if (out && capacity >= size) std::memcpy(out, text.c_str(), size);
    return size;
}
static void* GetModuleHandleW(const wchar_t*) { return installed ? reinterpret_cast<void*>(1) : nullptr; }
static void* GetProcAddress(void*, const char* name) {
    if (std::string_view(name) == "PE_GetApiVersion") return reinterpret_cast<void*>(&Version);
    if (std::string_view(name) == "PE_GetSnapshotJSON") return reinterpret_cast<void*>(&Read);
    if (journalExport && std::string_view(name) == "PE_GetIntelInitiatives080JSON") return reinterpret_cast<void*>(&ReadJournal);
    if (sidesExport && std::string_view(name) == "PE_GetPoliticalSides080JSON") return reinterpret_cast<void*>(&ReadSides);
    return nullptr;
}
''' + source + r'''
static void Require(bool b, const char* message) { if (!b) throw std::runtime_error(message); }
static json Load(const char* path) { std::ifstream file(path); json value; file >> value; return value; }
int main(int argc, char** argv) { try {
    const auto before = Load(argv[1]), after = Load(argv[2]);
    auto* bridge = IntelEngine::PoliticalEngineBridge::GetSingleton();
    if (argc>3 && std::string(argv[3])=="nojournal") { journalExport=false; argc=3; }
    if (argc>3 && std::string(argv[3])=="nosides") { sidesExport=false; argc=3; }
    for (unsigned version : {0u, 2u, 4u}) {
        api = version; state = before;
        // Separate processes exercise the one-shot detection gate.
        if (argc > 3 && std::string(argv[3]) == std::to_string(version)) {
            bridge->Initialize(); bridge->MarkDirty();
            Require(bridge->Poll() == json::array() && !bridge->IsActive(), "old/wrong API emitted changes");
            std::cout << "PASS incompatible API " << version << '\n'; return 0;
        }
    }
    if (argc > 3) {
        installed = false; bridge->Initialize(); bridge->MarkDirty();
        Require(bridge->Poll() == json::array() && !bridge->IsActive(), "absent PE emitted changes");
        std::cout << "PASS absent PE\n"; return 0;
    }
    api = 3; state = before; bridge->Initialize();
    Require(bridge->Poll() == json::array() && bridge->IsActive(), "attachment replayed news");
    Require(bridge->Context()["territory"]["11"]["owner"]==3 && bridge->Context()["realms"]["9"]=="Fellburg", "active public territory context");
    if(sidesExport) Require(bridge->Context()["territory"]["11"]["side_id"]==1,"original current owner side from optional export");
    else Require(!bridge->Context()["territory"]["11"].contains("side_id"),"missing export must not invent district side");
    state = after; bridge->MarkDirty(); auto changes = bridge->Poll();
    Require(changes.size() == 1 && changes[0]["type"] == "pe_territory" &&
        changes[0]["changes"] == json::array({{{"district","11"},{"name","Southern Reach"},{"from",3},{"to",9}}}), "real owner transition");
    Require(IntelEngine::PEEventText(changes[0]) == "Fellburg took Southern Reach from Markarth.", "factual realm-name rumor");
    if(sidesExport) {
        Require(bridge->Context()["territory"]["11"]["side_id"]==2,"captured district follows current owner, not original hold");
        sides["realms"][1]["side_id"]=3;
        SKSE::ModCallbackEvent sideChanged{"PL_StateChanged","[\"political_sides080\"]"};
        bridge->ProcessEvent(&sideChanged,nullptr);
        Require(bridge->Poll()==json::array() && bridge->Context()["territory"]["11"]["side_id"]==3,"sides-only event refreshes context without news");
        sides["realms"][1]["side_id"]=0;
        bridge->MarkDirty(); bridge->Poll();
        Require(bridge->Context()["territory"]["11"]["side_id"]==0,"independent is a known side, not missing");
        sides["realms"][1]["side_id"]=7;
        bridge->MarkDirty(); bridge->Poll();
        Require(!bridge->Context()["territory"]["11"].contains("side_id"),"invalid allegiance omitted");
        const auto valid=sides; sides=nullptr; bridge->MarkDirty(); bridge->Poll();
        Require(!bridge->Context()["territory"]["11"].contains("side_id") &&
                bridge->Context()["side_source"]=="political_sides080", "malformed present export never falls back");
        sides=valid; sides["realms"][1]["side_id"]=2;
        bridge->MarkDirty(); bridge->Poll();
    }
    bridge->MarkDirty(); Require(bridge->Poll() == json::array(), "duplicate re-read");
    state = before; SKSE::ModCallbackEvent loaded{"PL_StateChanged", "[\"loaded\"]"};
    bridge->ProcessEvent(&loaded, nullptr); Require(bridge->Poll() == json::array(), "load replayed news");
    // LABELLED SYNTHETIC six-district turn, using the genuine before capture.
    state = before; state["revision"] = 100;
    for (int i=0;i<6;++i) state["campaign070"]["regions"][i]["owner_realm_id"] = 9;
    bridge->MarkDirty(); changes = bridge->Poll();
    Require(changes.size()==1 && changes[0]["changes"].size()==6, "six districts must combine");
    state = before; state["status"]["bEnabled"] = false; bridge->MarkDirty();
    Require(bridge->Poll()==json::array() && !bridge->IsActive(), "disabled PE");
    Require(bridge->Context().is_null(), "disabled context omitted");
    state = after; bridge->MarkDirty(); Require(bridge->Poll()==json::array(), "reenable replay");
    state = before; bridge->MarkDirty(); bridge->Poll(); Require(bridge->Context().is_object(), "active before integration toggle");
    bridge->SetEnabled(false); state = after;
    Require(bridge->Context().is_null() && bridge->Poll()==json::array() && !bridge->IsActive(), "integration off: no context, no changes");
    bridge->SetEnabled(true); Require(bridge->Poll()==json::array() && bridge->IsActive(), "integration on: fresh baseline, no replay");
    state["data_ready"] = false; bridge->MarkDirty();
    Require(bridge->Poll()==json::array() && !bridge->IsActive(), "not-ready PE");
    state = before; bridge->MarkDirty(); Require(bridge->Poll()==json::array(), "ready baseline");
    state = after; resetDuringRead = bridge; bridge->MarkDirty();
    Require(bridge->Poll()==json::array() && !bridge->IsActive(), "load during export read");
    Require(bridge->Poll()==json::array(), "post-race baseline");
    state=after; state["revision"]=200;
    for (int i=0;i<8;++i) state["rulers"][i]["actor_id"] = state["rulers"][i]["actor_id"].get<unsigned>()+1;
    bridge->MarkDirty(); Require(bridge->Poll().size()==4, "four-record revision cap");
    state["campaign070"]["regions"][0]["owner_realm_id"]=9; bridge->MarkDirty();
    Require(bridge->Poll()==json::array(), "cap applies across reads of one revision");
    bridge->MarkDirty(true); Require(bridge->Context().is_null(), "load immediately hides context");
    Require(bridge->Poll()==json::array(), "load baseline at same revision");
    state["campaign070"]["regions"][1]["owner_realm_id"]=9; bridge->MarkDirty();
    Require(bridge->Poll().size()==1, "load resets revision accounting");
    if (journalExport) {
        state=before; state["revision"]=300; bridge->MarkDirty(true);
        Require(bridge->Poll()==json::array(), "initial initiative journal baseline");
        journal["intel_initiatives080"]["events"].push_back({{"id","new"},{"event_type","pe_visit"},{"source",20},{"target",104641},{"relation_delta",0},{"text","Rulers met for a public visit."}});
        state["revision"]=301; bridge->MarkDirty(); changes=bridge->Poll();
        Require(changes.size()==1 && changes[0]["type"]=="pe_visit" &&
                IntelEngine::PEEventText(changes[0])=="Rulers met for a public visit.", "new public initiative must be ingested exactly once");
        bridge->MarkDirty(); Require(bridge->Poll()==json::array(), "journal ID dedupe");
        bridge->MarkDirty(true); Require(bridge->Poll()==json::array(), "loaded journal baseline");
        journal["intel_initiatives080"]["events"].push_back({{"id","bad"},{"event_type","pe_gift"},{"source",20},{"target",104641},{"relation_delta",7},{"text","Invalid delta"}});
        state["revision"]=302; bridge->MarkDirty(); Require(bridge->Poll()==json::array(), "initiative cannot move relations");
    }
    using IntelEngine::PEComparable; using IntelEngine::PEDiff;
    auto base = PEComparable(before);
    auto synthetic = before; synthetic["campaign080"]["wars"]["wars"][0]["phase"] = 0;
    Require(PEDiff(base, PEComparable(synthetic))==json::array(), "ultimatum is not war news");
    synthetic = before; auto& war = synthetic["campaign080"]["wars"]["wars"][0];
    war["phase"]=3; war["settlement_applied"]=true; war["applied_turn"]=39;
    war["settlement"]={{"outcome","white_peace"}};
    changes=PEDiff(base,PEComparable(synthetic));
    Require(changes.size()==1 && changes[0]["type"]=="pe_campaign", "applied peace cessation");
    war["settlement"]["outcome"]="puppet";
    Require(PEDiff(base,PEComparable(synthetic))==json::array(), "private puppet settlement");
    synthetic=after; synthetic["campaign080"]["governments"][9]["kind"]=2;
    Require(PEDiff(base,PEComparable(synthetic))==json::array(), "private puppet ownership");
    for (const auto& malformed : {json(),json::array(),json("bad"),json(5)}) {
        synthetic=before; synthetic["campaign070"]=malformed; synthetic["rulers"]=malformed;
        synthetic["campaign080"]=malformed;
        Require(PEDiff(base,PEComparable(synthetic))==json::array(), "malformed section emitted news");
    }
    std::cout << "PASS production bridge: real owner, load baseline, duplicate, synthetic six-district batch, inactivity, load fence, peace/privacy, malformed sections\n";
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
'''
    (work / "check.cpp").write_text(code)
    subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", "/I"+str(work),
                    "/I"+str(src), "/I"+str(deps/"include"), "check.cpp", "/Fe:check.exe"], cwd=work, check=True)
    args = [str(work/"check.exe"), str((samples/"before-snapshot.json").resolve()), str((samples/"after-snapshot.json").resolve())]
    for suffix in ([], ["nojournal"], ["nosides"], ["0"], ["2"], ["4"], ["absent"]):
        subprocess.run(args + suffix, cwd=work, check=True)
