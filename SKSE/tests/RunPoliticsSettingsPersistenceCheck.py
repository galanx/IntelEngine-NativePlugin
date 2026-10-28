"""Compile the real politics settings persistence methods without the game ABI.

Usage (VS developer shell): python SKSE/tests/RunPoliticsSettingsPersistenceCheck.py
"""
import pathlib
import subprocess
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / "src"


def method(text, signature):
    start = text.index("    " + signature)
    brace = text.index("{", start)
    depth = 0
    for end in range(brace, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[start:end + 1]
    raise ValueError(f"unterminated method: {signature}")


dashboard = (src / "DashboardConfig.cpp").read_text(encoding="utf-8")
politics = (src / "FactionPolitics.cpp").read_text(encoding="utf-8")
methods = [method(dashboard, "std::string DashboardConfig::ReadYamlString("),
           method(dashboard, "bool DashboardConfig::WriteYamlValue("),
           *(method(politics, signature) for signature in (
               "bool FactionPolitics::SetEnabled(",
               "bool FactionPolitics::SetTickIntervalHours(",
               "bool FactionPolitics::SetMaxRelationChangePerTick(",
               "bool FactionPolitics::SetMaxActiveWars(",
               "void FactionPolitics::LoadSettings(",
               "void FactionPolitics::Initialize("))]

with tempfile.TemporaryDirectory(prefix="politics-settings-") as directory:
    work = pathlib.Path(directory)
    fixture = work / "Data" / "SKSE" / "Plugins" / "SkyrimNet" / "config" / "plugins" / "IntelEngine" / "settings.yaml"
    fixture.parent.mkdir(parents=True)
    (work / "check.cpp").write_text(r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_set>
namespace logger {
template<class... T> void info(T&&...) {}
template<class... T> void warn(T&&...) {}
template<class... T> void error(T&&...) {}
}
namespace IntelEngine {
class PoliticalDB {
public:
    static PoliticalDB* GetSingleton() { static PoliticalDB value; return &value; }
    bool IsReady() const { return true; }
};
std::string settingsPath;
class DashboardConfig {
public:
    static DashboardConfig* GetSingleton() { static DashboardConfig value; return &value; }
    std::string GetSettingsPath() { return settingsPath; }
    std::string ReadYamlString(const std::string&, const std::string&, const std::string&, const std::string&);
    bool WriteYamlValue(const std::string&, const std::string&, const std::string&);
private:
    std::mutex writeMutex_;
};
class FactionPolitics {
public:
    bool SetEnabled(bool);
    bool SetTickIntervalHours(int);
    bool SetMaxRelationChangePerTick(int);
    bool SetMaxActiveWars(int);
    void LoadSettings();
    void Initialize();
    bool IsEnabled() const { return enabled_.load(); }
    bool IsInitialized() const { return initialized_.load(); }
    bool IsConfigLoaded() const { return configLoaded_; }
    int GetTickIntervalHours() const { return tickIntervalHours_.load(); }
    int GetMaxRelationChangePerTick() const { return maxRelationChangePerTick_.load(); }
    int GetMaxActiveWars() const { return maxActiveWars_.load(); }
    bool LoadConfig() { configLoaded_ = true; return true; }
    void SeedDefaultRelations() {}
    void RecalculateRelationScores() {}
    void SnapshotCrimeGoldBaseline() {}
    void WritePoliticalStateFile() {}
private:
    std::atomic<bool> enabled_{true};
    std::atomic<bool> initialized_{false};
    bool configLoaded_ = false;
    std::atomic<int> tickIntervalHours_{6};
    std::atomic<int> maxRelationChangePerTick_{15};
    std::atomic<int> maxActiveWars_{2};
    std::atomic<int> warDeclarationCooldownDays_{7};
    std::atomic<int> moraleDecayPerTick_{2};
    std::unordered_set<int> vanillaSynced_;
};
''' + "\n".join(methods) + r'''
}
int main(int argc, char** argv) {
    using namespace IntelEngine;
    assert(argc == 2);
    settingsPath = argv[1];
    {
        std::ofstream file(settingsPath);
        file << "politics:\n  enabled: false\n  tick_interval_hours: 6\n"
                "  max_relation_change_per_tick: 15\n  max_active_wars: 2\n";
    }
    FactionPolitics politics;
    politics.Initialize();
    assert(!politics.IsEnabled());
    assert(politics.IsInitialized());
    assert(politics.IsConfigLoaded());
    assert(politics.SetEnabled(true));
    assert(politics.IsEnabled());
    assert(politics.SetEnabled(false));
    assert(!politics.IsEnabled());
    assert(politics.SetTickIntervalHours(1));
    assert(politics.SetTickIntervalHours(168));
    assert(!politics.SetTickIntervalHours(169));
    assert(politics.GetTickIntervalHours() == 168);
    assert(politics.SetMaxRelationChangePerTick(5));
    assert(politics.SetMaxRelationChangePerTick(30));
    assert(!politics.SetMaxRelationChangePerTick(31));
    assert(politics.GetMaxRelationChangePerTick() == 30);
    assert(politics.SetMaxActiveWars(1));
    assert(politics.SetMaxActiveWars(5));
    assert(!politics.SetMaxActiveWars(0));
    assert(politics.GetMaxActiveWars() == 5);
    settingsPath = "missing/settings.yaml";
    assert(!politics.SetEnabled(true));
    assert(!politics.IsEnabled());
    settingsPath = argv[1];
    FactionPolitics reloaded;
    reloaded.LoadSettings();
    assert(!reloaded.IsEnabled());
    assert(reloaded.GetTickIntervalHours() == 168);
    assert(reloaded.GetMaxRelationChangePerTick() == 30);
    assert(reloaded.GetMaxActiveWars() == 5);
}
''', encoding="utf-8")
    executable = work / "check.exe"
    subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    str(work / "check.cpp"), "-o", str(executable)], check=True)
    subprocess.run([str(executable), "Data/SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/settings.yaml"],
                   cwd=work, check=True)
