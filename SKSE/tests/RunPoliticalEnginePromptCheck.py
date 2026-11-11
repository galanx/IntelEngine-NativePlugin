"""Render production political prompts with the existing SkyrimNet Inja header.
Usage: python SKSE/tests/RunPoliticalEnginePromptCheck.py <triplet> <inja-single-include>
Baseline templates come from origin/dev, not copied fixtures.
"""
import pathlib
import subprocess
import sys
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
base = "SKSE/Plugins/SkyrimNet/external/galanx.intelengine/prompts/"
with tempfile.TemporaryDirectory(prefix="pe-prompts-") as directory:
    work = pathlib.Path(directory)
    names = ["submodules/character_bio/0810_intel_political_awareness.prompt", "intel_political_dm.prompt"]
    for i, name in enumerate(names):
        (work / f"current{i}.prompt").write_bytes((repo / (base+name)).read_bytes())
        data = subprocess.check_output(["git", "show", "origin/dev:"+base+name], cwd=repo)
        if i == 1:  # fix38 rewords two non-PE lines on purpose; apply it to the origin/dev baseline so only PE gating is compared
            data = data.replace(b"warring factions may appear in consecutive ticks for battle_result or surrender events;",
                b"warring factions may appear in consecutive ticks, but a war gets at most ONE battle_result in any two consecutive ticks \xe2\x80\x94 on the tick after a battle, that pair may appear only for surrender, otherwise pick a different pair or return should_act:false;")
            data = data.replace(b"- **Generate battle_result events** for active wars ",
                b"- **Generate battle_result events** for active wars (subject to Rule 8: never two in a row for one war; never reuse the previous battle's `battle_location` for the same war) ")
        (work / f"baseline{i}.prompt").write_bytes(data)
    (work / "check.cpp").write_text(r'''
#include <inja/inja.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
using nlohmann::json;
static void Require(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
static std::string Read(const char* name) { std::ifstream f(name); return {std::istreambuf_iterator<char>(f),{}}; }
int main() { try {
    inja::Environment env;
    env.set_line_statement("");
    env.set_search_included_templates_in_files(false);
    json political = {{"factions",json::array()},{"relations",json::array()},
        {"recent_events",json::array({{{"type","war_declaration"},{"description","Existing war news"},{"faction_a","A"}}})}};
    env.add_callback("read_json",1,[&](inja::Arguments&) { return political; });
    env.add_callback("existsIn",2,[](inja::Arguments& a) { return a[0]->is_object() && a[0]->contains(a[1]->get<std::string>()); });
    env.add_callback("append",2,[](inja::Arguments& a) { auto v=*a[0]; v.push_back(*a[1]); return v; });
    env.add_callback("contains",2,[](inja::Arguments& a) { return a[0]->get<std::string>().find(a[1]->get<std::string>())!=std::string::npos; });
    env.add_callback("isValidActor",1,[](inja::Arguments&) { return false; });
    env.add_callback("decnpc",1,[](inja::Arguments&) { return json{{"name","Observer"}}; });
    env.add_callback("get_location",1,[](inja::Arguments&) { return "Whiterun"; });
    env.add_callback("is_in_faction",2,[](inja::Arguments&) { return false; });
    json data={{"render_mode","full"},{"actorUUID",""},{"player",{{"name","Player"}}}};
    const auto npc = Read("current0.prompt"), oldNpc = Read("baseline0.prompt");
    auto baseline=env.render(oldNpc,data);
    Require(baseline.find("Existing war news")!=std::string::npos,"nontrivial NPC baseline");
    Require(env.render(npc,data)==baseline,"inactive NPC prompt changed bytes");
    political["recent_events"].push_back({{"type","pe_territory"},{"faction_a",""},{"description","Fellburg took Southern Reach from Markarth."}});
    auto result=env.render(npc,data);
    Require(result.find("Fellburg took Southern Reach from Markarth.")!=std::string::npos,"unaffiliated NPC cannot hear PE news");
    political["recent_events"].back()["type"]="pe_visit";
    Require(env.render(npc,data).find("Fellburg took Southern Reach from Markarth.")!=std::string::npos,"public initiative visibility");
    json dm={{"politicalContext",json{{"recent_events",json::array()}}.dump(2)},
        {"tickHours","6"},{"maxActiveWars","2"},{"maxRelationChange","15"},{"peActive",false}};
    const auto dmTemplate=Read("current1.prompt"), oldDm=Read("baseline1.prompt");
    auto inactive=env.render(dmTemplate,dm);
    Require(inactive==env.render(oldDm,dm) && inactive.find("Study the political state")!=std::string::npos,
            "inactive DM prompt must remain byte-identical");
    dm["peActive"]=true;
    Require(env.render(dmTemplate,dm).find("political_reaction")!=std::string::npos &&
            env.render(dmTemplate,dm).find("reacts_to")!=std::string::npos, "active DM reaction rule");
    dm["politicalContext"]="";
    Require(env.render(dmTemplate,dm)==env.render(oldDm,dm), "empty context sentinel remains unchanged");
    std::cout << "PASS real Inja: inactive NPC prompt byte-identical; unaffiliated PE territory/initiative rumors visible\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
''')
    subprocess.run(["cl","/nologo","/EHsc","/std:c++latest","/I"+str(pathlib.Path(sys.argv[1])/"include"),
                    "/I"+sys.argv[2],"check.cpp","/Fe:check.exe"],cwd=work,check=True)
    subprocess.run([str(work/"check.exe")],cwd=work,check=True)
