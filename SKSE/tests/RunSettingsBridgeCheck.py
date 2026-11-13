"""Compile the real SettingsBridge source and check its rules; keep manifest.yaml's generated fields in step.
Usage: python RunSettingsBridgeCheck.py <triplet dir with nlohmann/json.hpp> [--write]
  --write  replace the generated block of manifest.yaml (between the two marker lines) with the table's fields.
Without it the block must already equal what the table generates.
"""
import pathlib
import subprocess
import sys
import tempfile

deps = pathlib.Path(sys.argv[1])
write = '--write' in sys.argv[2:]
skse = pathlib.Path(__file__).resolve().parents[1]
src = skse / 'src'
manifest = skse / 'Plugins' / 'SkyrimNet' / 'config' / 'plugins' / 'IntelEngine' / 'manifest.yaml'
BEGIN = '    # >>> settings shared with the game: generated from SKSE/src/SettingsBridge.cpp (SKSE/tests/RunSettingsBridgeCheck.py --write)\n'
END = '    # <<< settings shared with the game\n'

with tempfile.TemporaryDirectory(prefix='settings-bridge-') as directory:
    work = pathlib.Path(directory)
    (work / 'SettingsBridge.h').write_text((src / 'SettingsBridge.h').read_text(encoding='utf-8'), encoding='utf-8')
    (work / 'SettingsBridge.cpp').write_text((src / 'SettingsBridge.cpp').read_text(encoding='utf-8'), encoding='utf-8')
    (work / 'check.cpp').write_text(r'''#include "SettingsBridge.cpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
using namespace IntelEngine::SettingsBridge;
using nlohmann::json;

int main(int, char** argv) {
    std::map<std::string, std::string> file;
    Store store;
    store.read = [&](const std::string& s, const std::string& n, std::string& v) {
        auto it = file.find(s + "." + n);
        if (it == file.end()) return false;
        v = it->second;
        // as the yaml reader hands it over: without the quotes
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return true;
    };
    store.write = [&](const std::string& s, const std::string& n, const std::string& v) { file[s + "." + n] = v; return true; };

    // The game's settings as Core.BuildConfigJson sends them: every table key, at its default.
    json config = json::object();
    for (const Entry& e : Table()) {
        if (e.kind == Kind::Bool) config[e.key] = e.def > 0.5;
        else config[e.key] = e.def;
    }
    config["taskConfirmPrompt"] = true;          // a key the table does not know is ignored

    // 1. Game wins: an empty file receives every setting, formatted for yaml; a second pass writes nothing.
    assert(SyncToFile(config, store) == static_cast<int>(Table().size()));
    assert(file.size() == Table().size());
    assert(file["stories.enabled"] == "true");
    assert(file["stories.check_interval_hours"] == "3");
    assert(file["social.interval_hours"] == "1.5");
    assert(file["tasks.max_concurrent"] == "5");
    assert(file["stories.reach_informant"] == "\"Same town (everyone)\"");
    assert(file["tasks.confirm_schedule_meeting"] == "\"Everyone\"");
    assert(file["stories.danger_zone_policy"] == "\"Block civilians only\"");
    assert(file.count("tasks.taskConfirmPrompt") == 0);
    assert(SyncToFile(config, store) == 0);

    // 2. File equals game: nothing to apply.
    assert(ApplyFromFile(config, store).empty());

    // 3. File wins: changed values come back as the dashboard's own setting events.
    file["stories.enabled"] = "false";
    file["tasks.max_concurrent"] = "3";
    file["stories.reach_informant"] = "\"Same hold (everyone)\"";
    file["stories.type_ambush"] = "false";
    file["social.type_gossip"] = "no";
    file["stories.check_interval_hours"] = "999";           // past the range: clamped
    file["tasks.confirm_fetch_person"] = "\"Nobody\"";      // not one of the choices: the game keeps its value
    file["meetings.timeout_hours"] = "soon";                // not a number: the game keeps its value
    file["tasks.confirm_go_to_location"] = "2";             // a choice given by position
    auto changes = ApplyFromFile(config, store);
    std::map<std::string, std::pair<std::string, float>> byArg;
    for (const auto& c : changes) byArg[c.arg] = {c.event, c.value};
    assert(changes.size() == 7);
    assert(byArg["storyEnabled"].first == "IntelEngine_DashboardSetting" && byArg["storyEnabled"].second == 0.0f);
    assert(byArg["maxTasks"].second == 3.0f);
    assert(byArg["holdPolicyInformant"].second == 3.0f);
    assert(byArg["ambush"].first == "IntelEngine_DashboardToggleStory" && byArg["ambush"].second == 0.0f);
    assert(byArg["npc_gossip"].first == "IntelEngine_DashboardToggleStory" && byArg["npc_gossip"].second == 0.0f);
    assert(byArg["storyInterval"].second == 168.0f);
    assert(byArg["confirmGoToLocation"].second == 2.0f);
    assert(byArg.count("confirmFetchPerson") == 0 && byArg.count("meetingTimeoutHours") == 0);

    // 4. Once the game has taken them over, nothing is left to apply, and a sync leaves the valid file values alone
    //    (the two unreadable ones are rewritten from the game).
    config["storyEnabled"] = false; config["maxTasks"] = 3; config["holdPolicyInformant"] = 3; config["typeAmbush"] = false;
    config["typeNpcGossip"] = false; config["storyInterval"] = 168; config["confirmGoToLocation"] = 2;
    assert(ApplyFromFile(config, store).empty());
    assert(SyncToFile(config, store) == 2);      // the two unreadable ones: "Nobody" and "soon"
    assert(file["tasks.confirm_fetch_person"] == "\"Followers only\"");
    assert(file["meetings.timeout_hours"] == "5");
    assert(file["stories.check_interval_hours"] == "999");     // reads as the clamped 168, which is what the game has
    assert(file["tasks.confirm_go_to_location"] == "2");       // valid by position: left as the player wrote it

    // 5. A key the file lacks is filled in from the game and changes nothing in the game.
    file.erase("tasks.release_distance");
    assert(ApplyFromFile(config, store).empty());
    assert(file["tasks.release_distance"] == "800");

    // 6. Scripts that do not report a setting: nothing is written for it; a file value is still applied.
    json older = config;
    older.erase("releaseDistance");
    file["tasks.release_distance"] = "650";
    assert(SyncToFile(older, store) == 0);
    changes = ApplyFromFile(older, store);
    assert(changes.size() == 1 && changes[0].arg == "releaseDistance" && changes[0].value == 650.0f);

    // 7. No config at all, or a store without access.
    assert(SyncToFile(json::array(), store) == 0);
    assert(ApplyFromFile(json(), store).empty());
    assert(SyncToFile(config, Store{}) == 0);

    // 8. The table itself: unique keys and paths, defaults inside their ranges, choices not empty.
    std::map<std::string, int> keys, paths;
    for (const Entry& e : Table()) {
        assert(++keys[e.key] == 1);
        assert(++paths[std::string(e.section) + "." + e.name] == 1);
        if (e.kind == Kind::Int || e.kind == Kind::Float) assert(e.min <= e.def && e.def <= e.max && e.min < e.max);
        if (e.kind == Kind::Select) assert(OptionsOf(e).size() >= 2 && e.def >= 0 && e.def < OptionsOf(e).size());
        assert((e.event == nullptr) == (e.eventArg == nullptr));
        double back = -1;
        assert(FromYaml(e, ToYaml(e, e.def).substr(e.kind == Kind::Select ? 1 : 0, ToYaml(e, e.def).size() - (e.kind == Kind::Select ? 2 : 0)), back));
        assert(back == e.def);
    }

    std::ofstream(argv[1], std::ios::binary) << ManifestFields();
    std::cout << "PASS: sync, apply, clamping, unreadable values, missing keys, table (" << Table().size() << " settings)\n";
}
''', encoding='utf-8')
    exe = work / 'check'
    fields = work / 'fields.yaml'
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-I', str(deps / 'include'), '-I', str(work),
                    str(work / 'check.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(fields)], check=True)
    generated = fields.read_bytes().decode('utf-8').replace('\r\n', '\n')

raw = manifest.read_bytes()
crlf = b'\r\n' in raw
text = raw.decode('utf-8').replace('\r\n', '\n')
assert text.count(BEGIN) == 1 and text.count(END) == 1, 'manifest.yaml lacks the two marker lines of the generated block'
head, rest = text.split(BEGIN)
block, tail = rest.split(END)
if write:
    out = head + BEGIN + generated + END + tail
    manifest.write_bytes((out.replace('\n', '\r\n') if crlf else out).encode('utf-8'))
    print('manifest.yaml: generated block written (%d lines)' % generated.count('\n'))
else:
    assert block == generated, 'manifest.yaml is out of step with SettingsBridge.cpp: run this check with --write'
    print('PASS: manifest.yaml lists exactly the table\'s settings')
