"""Execute production conduct/lookup methods with real SQLite and modeled game edges.
Usage (VS shell): python SKSE/tests/RunPlayerConductCheck.py <vcpkg-triplet-dir>
"""
import pathlib
import subprocess
import sys
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / 'src'
deps = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='conduct-check-') as directory:
    work = pathlib.Path(directory)
    (work / 'Plugin.h').write_text('''#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace RE {
using FormID = std::uint32_t;
struct Actor {
 std::string faction;
 // ProcessPlayerConduct logs who reported (the [diag:conduct] line).
 const char* GetDisplayFullName() const { return "Reporter"; }
 FormID GetFormID() const { return 0x14; }
};
inline std::vector<std::string> notifications;
struct Calendar {
 static Calendar* GetSingleton() { static Calendar clock; return &clock; }
 float GetCurrentGameTime() const { return 2.f; }
};
namespace SendHUDMessage {
inline void ShowHUDMessage(const char* text) { notifications.emplace_back(text); }
}
}
namespace logger {
template<class... T> void debug(T&&...) {}
template<class... T> void info(T&&...) {}
template<class... T> void warn(T&&...) {}
template<class... T> void error(T&&...) {}
}
''', encoding='utf-8')
    for name in ('PoliticalDB.h', 'PoliticalDB.cpp', 'FactionPolitics.h'):
        text = (src / name).read_text(encoding='utf-8')
        if name == 'FactionPolitics.h':
            text = text.replace('    private:', '    public:')
        (work / name).write_text(text, encoding='utf-8')
    source = (src / 'FactionPolitics.cpp').read_text(encoding='utf-8')
    methods = []
    for signature in ('int FactionPolitics::ProcessPlayerConduct(',
                      'std::optional<FactionConfig> FactionPolitics::GetFaction(',
                      'std::string FactionPolitics::GetFactionIdByName('):
        start = source.index('    ' + signature)
        end = source.index('\n    }', start) + len('\n    }')
        methods.append(source[start:end])
    (work / 'methods.inc').write_text('namespace IntelEngine {\n' + '\n'.join(methods) + '''
std::string FactionPolitics::GetNPCFactionId(RE::Actor* actor) const { return actor ? actor->faction : ""; }
}\n''', encoding='utf-8')
    (work / 'check.cpp').write_text('''#include "FactionPolitics.h"
#include <cctype>
#include <iostream>
#include <stdexcept>
static std::string ToLower(std::string value) {
 for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
 return value;
}
#include "methods.inc"
static void require(bool ok) { if (!ok) throw std::runtime_error("conduct regression"); }
int main() {
 using namespace IntelEngine;
 auto* p = FactionPolitics::GetSingleton();
 auto* db = PoliticalDB::GetSingleton();
 FactionConfig target; target.id = "ThalmorFaction"; target.name = "the Thalmor";
 FactionConfig rival; rival.id = "Rival"; rival.rivals = {target.id};
 FactionConfig ally; ally.id = "Ally"; ally.allies = {target.id};
 p->factions_ = {target, rival, ally};
 p->factionIndex_ = {{target.id, 0}, {rival.id, 1}, {ally.id, 2}};
 p->nameToId_ = {{"the thalmor", target.id}};
 RE::Actor reporter;
 auto call = [&](const std::string& faction, const std::string& sentiment, int count, const std::string& notice) {
   RE::notifications.clear();
   require(p->ProcessPlayerConduct(&reporter, faction, sentiment, "test") == count);
   require(RE::notifications == std::vector<std::string>{notice});
 };
 call(target.id, "positive", 0, "Conduct report failed: politics is not initialized.");
 p->initialized_ = true;
 call(target.id, "", 0, "Conduct report failed: invalid sentiment ''.");
 call("Missing", "positive", 0, "Conduct report failed: unknown faction 'Missing'.");
 call(target.id, "positive", 0, "Conduct report failed: political database is unavailable.");
 require(db->Initialize("./conduct.db"));
 call(target.id, "positive", 1, "Reported to the Thalmor: 1 standings changed.");
 require(db->GetPlayerStanding(target.id) == 5);
 call("THE THALMOR", "negative", 1, "Reported to the Thalmor: 1 standings changed.");
 require(db->GetPlayerStanding(target.id) == 0);
 reporter.faction = rival.id;
 call(target.id, "strongly_positive", 2, "Reported to the Thalmor: 2 standings changed.");
 require(db->GetPlayerStanding(target.id) == 10 && db->GetPlayerStanding(rival.id) == -5);
 reporter.faction = ally.id;
 call(target.id, "strongly_negative", 2, "Reported to the Thalmor: 2 standings changed.");
 require(db->GetPlayerStanding(target.id) == 0 && db->GetPlayerStanding(ally.id) == -5);
 reporter.faction = target.id;
 call(target.id, "positive", 1, "Reported to the Thalmor: 1 standings changed.");
 require(db->GetPlayerStanding(target.id) == 5 && db->GetPlayerStanding(rival.id) == -5);
 call("Missing", "positive", 0, "Conduct report failed: unknown faction 'Missing'.");
 require(db->GetPlayerStanding(target.id) == 5);
 db->Shutdown();
 std::cout << "PASS: production conduct ID/name lookup, same-faction handling, four sentiments, primary/rival/ally changes and notifications\\n";
}
''', encoding='utf-8')
    executable = work / 'check.exe'
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS',
                    '-I' + str(work), '-I' + str(src), '-isystem', str(deps / 'include'),
                    str(work / 'check.cpp'), str(work / 'PoliticalDB.cpp'),
                    str(deps / 'lib/sqlite3.lib'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], cwd=work, check=True)
