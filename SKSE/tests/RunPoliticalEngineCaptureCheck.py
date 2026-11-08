"""Exercise production capture offline; run in a VS2022 developer shell.
Usage: python SKSE/tests/RunPoliticalEngineCaptureCheck.py <vcpkg-triplet-dir>
Only the logger, export reader and bridge shell are modeled; Capture is verbatim.
"""
import pathlib
import subprocess
import sys
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / "src"
deps = pathlib.Path(sys.argv[1]).resolve()
source = (src / "PoliticalEngineBridge.cpp").read_text(encoding="utf-8")
start = source.index("    void PoliticalEngineBridge::Capture(")
end = source.rfind("\n}")
with tempfile.TemporaryDirectory(prefix="pe-capture-") as directory:
    work = pathlib.Path(directory)
    code = r'''#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "PoliticalEngineReadJSON.h"
static int written = 0;
namespace logger { template<class... A> void info(const char*, A&&...) { ++written; } template<class... A> void warn(const char*, A&&...) {} }
namespace IntelEngine {
class PoliticalEngineBridge { public:
using Reader = std::uint32_t (*)(char*,std::uint32_t);
Reader reader_; std::atomic<bool> active_{false};
void Capture(const std::string& sections);
};
''' + source[start:end] + r'''
}
static std::string payload;
static std::uint32_t Read(char* out,std::uint32_t capacity) {
    auto n=static_cast<std::uint32_t>(payload.size()+1);
    if(out && capacity>=n) std::memcpy(out,payload.c_str(),n);
    return n;
}
static void Require(bool b,const char* message) { if(!b) throw std::runtime_error(message); }
int main() { try {
    auto folder=std::filesystem::path("Data/SKSE/Plugins/IntelEngine/pe_snapshots");
    IntelEngine::PoliticalEngineBridge bridge; bridge.reader_=Read;
    payload=R"({"revision":1,"data_ready":true,"status":{"bEnabled":true}})";
    bridge.Capture("attached"); bridge.Capture("repeat");
    Require(bridge.active_,"typed readiness true"); Require(written==1,"repeated snapshot path logged twice");
    std::ifstream file(folder/"pe_snapshot_1.json"); nlohmann::json saved; file>>saved;
    Require(saved==nlohmann::json::parse(payload),"snapshot roundtrip");
    file.close();
    for(int i=2;i<=52;++i) { payload=nlohmann::json({{"revision",i}}).dump(); bridge.Capture("retention"); }
    int count=0; for(auto& e:std::filesystem::directory_iterator(folder)) if(e.path().extension()==".json") ++count;
    Require(count==50,"retention cap"); Require(!bridge.active_,"missing readiness inactive");
    payload=R"({"data_ready":1,"status":{"bEnabled":true}})"; bridge.Capture("bad type"); Require(!bridge.active_,"non-bool readiness");
    payload="[]"; bridge.Capture("non-object");
    bool utc=false; for(auto& e:std::filesystem::directory_iterator(folder)) {auto s=e.path().filename().string(); utc|=s.ends_with("Z.json");}
    Require(utc,"UTC fallback");
    std::ifstream events(folder/"pe_events.log"); std::string line; count=0; while(std::getline(events,line)) ++count;
    Require(count==55,"append-only event log");
    events.close();
    std::filesystem::remove(folder/"pe_events.log");
    std::filesystem::create_directory(folder/"pe_events.log");
    for(int i=100;i<104;++i) { payload=nlohmann::json({{"revision",static_cast<unsigned>(i)}}).dump(); bridge.Capture("log failure"); }
    count=0; for(auto& e:std::filesystem::directory_iterator(folder)) if(e.path().extension()==".json") ++count;
    Require(count==50,"retention must run even when event log cannot open");
    std::cout<<"PASS production Capture: roundtrip, readiness, repeated-path logging, retention50, UTC fallback, append log\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }}
'''
    (work / "check.cpp").write_text(code, encoding="utf-8")
    subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", "/I" + str(src),
                    "/I" + str(deps / "include"), "check.cpp", "/Fe:check.exe"],
                   cwd=work, check=True)
    subprocess.run([str(work / "check.exe")], cwd=work, check=True)
