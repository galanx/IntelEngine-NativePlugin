"""Render production political prompts with the existing SkyrimNet Inja header.
Usage: python SKSE/tests/RunPoliticalEnginePromptCheck.py <triplet> <inja-single-include>
Baseline templates come from origin/dev, not copied fixtures (IE_PROMPT_BASELINE names another ref).
"""
import os
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
        data = subprocess.check_output(["git", "show", os.environ.get("IE_PROMPT_BASELINE", "origin/dev")+":"+base+name], cwd=repo)
        if i == 1:  # fix38 rewords two non-PE lines on purpose; apply it to the origin/dev baseline so only PE gating is compared
            data = data.replace(b"warring factions may appear in consecutive ticks for battle_result or surrender events;",
                b"warring factions may appear in consecutive ticks, but a war gets at most ONE battle_result in any two consecutive ticks \xe2\x80\x94 on the tick after a battle, that pair may appear only for surrender, otherwise pick a different pair or return should_act:false;")
            data = data.replace(b"- **Generate battle_result events** for active wars ",
                b"- **Generate battle_result events** for active wars (subject to Rule 8: never two in a row for one war; never reuse the previous battle's `battle_location` for the same war) ")
            data = data.replace(b"- **Low morale triggers surrender**",  # fix48 adds the per-war battle flag rule
                b"- **No back-to-back battles** \xe2\x80\x94 never pick `battle_result` for an `active_wars` entry whose `battle_result_allowed_now` starts with \"no\"; choose another event instead.\n- **Low morale triggers surrender**")
            for o, n in [  # fix74: war mandate only for war_possible pairs
                (b'Only when the pair\'s status is "Critical" AND no active war', b'Only when the pair\'s status is "Critical" AND the pair has `war_possible: true` AND no active war'),
                (b"\xe2\x80\x94 war-eligible. If no active war exists and no cooldown applies, war_declaration MUST be your next event for this pair", b"\xe2\x80\x94 war-eligible only when `war_possible` is true (then, if no active war exists and no cooldown applies, war_declaration MUST be your next event for this pair); Critical pairs with `war_possible: false` use their factions' conflict styles and Rule 8 rotation"),
                (b"**Critical-pair interaction (Rule 3 below):**", b"**Critical-pair interaction (Rule 3 below, `war_possible: true` pairs only):**"),
                (b'If ANY faction pair has status "Critical"', b'If ANY faction pair with `war_possible: true` has status "Critical"'),
                (b"If multiple Critical pairs exist without wars", b"If multiple such pairs exist without wars"),
                (b'(b) if a pair\'s status is "Critical", war_declaration', b'(b) if a pair\'s status is "Critical" and `war_possible` is true, war_declaration'),
                # fix94: the conflict-style line names the war_possible condition too
                (b"(Imperial Legion, Stormcloaks, Thalmor): border_skirmish, war_declaration", b"(Imperial Legion, Stormcloaks, Thalmor): border_skirmish, and war_declaration only for pairs with `war_possible: true`")]:
                assert o in data, o
                data = data.replace(o, n)
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
    env.add_callback("isValidActor",1,[](inja::Arguments&) { return true; });
    std::string who="Observer", where="Whiterun";
    env.add_callback("decnpc",1,[&](inja::Arguments&) { return json{{"name",who}}; });
    env.add_callback("get_location",1,[&](inja::Arguments&) { return where; });
    env.add_callback("is_in_faction",2,[](inja::Arguments&) { return false; });
    json data={{"render_mode","full"},{"actorUUID","x"},{"player",{{"name","Player"}}}};
    const auto npc = Read("current0.prompt"), oldNpc = Read("baseline0.prompt");
    auto baseline=env.render(oldNpc,data);
    Require(baseline.find("Existing war news")!=std::string::npos,"nontrivial NPC baseline");
    Require(env.render(npc,data)==baseline,"inactive NPC prompt changed bytes");
    political["recent_events"].push_back({{"type","pe_territory"},{"faction_a",""},{"description","Fellburg took Southern Reach from Markarth."}});
    auto result=env.render(npc,data);
    Require(result.find("Fellburg took Southern Reach from Markarth.")!=std::string::npos,"unaffiliated NPC cannot hear PE news");
    political["recent_events"].back()["type"]="pe_visit";
    Require(env.render(npc,data).find("Fellburg took Southern Reach from Markarth.")!=std::string::npos,"public initiative visibility");
    // fix64: realm-only location fallback; public vs secret events
    political["factions"]={{{"id","CompanionsFaction"},{"name","The Companions"},{"hold","Whiterun"},{"leaders",json::array({"Vilkas"})},{"skyrim_faction_id","CompanionsFaction"}},
        {{"id","ThalmorFaction"},{"name","Thalmor"},{"hold","Solitude"},{"leaders",json::array({"Elenwen"})},{"skyrim_faction_id","ThalmorFaction"}},
        {{"id","PE_Whiterun"},{"name","Whiterun (Political Engine)"},{"hold","Whiterun"},{"leaders",json::array({"Iepe"})},{"type","political"}}};
    political["recent_events"]=json::array({
        {{"type","embargo"},{"faction_a","ThalmorFaction"},{"faction_b","SilverBloodFamily"},{"description","EMBARGO-NEWS"}},
        {{"type","espionage"},{"faction_a","ThalmorFaction"},{"faction_b","SilverBloodFamily"},{"description","SPY-NEWS"}}});
    auto civ=env.render(npc,data);
    Require(civ.find("territory of Whiterun (Political Engine)")!=std::string::npos,"PE_<Hold> fallback");
    Require(civ.find("Companions")==std::string::npos,"guild must not match by hold");
    Require(civ.find("EMBARGO-NEWS")!=std::string::npos,"civilian hears embargo");
    Require(civ.find("SPY-NEWS")==std::string::npos,"civilian must not hear espionage");
    who="Elenwen"; where="Solitude";
    auto elenwen=env.render(npc,data);
    Require(elenwen.find("leader of Thalmor")!=std::string::npos && elenwen.find("SPY-NEWS")!=std::string::npos && elenwen.find("EMBARGO-NEWS")!=std::string::npos,"party leader hears secret and public");
    // fix68: same type + same unordered pair shows once (newest text, count); other types and pe_* stay separate
    political["recent_events"]=json::array({
        {{"type","embargo"},{"faction_a","ThalmorFaction"},{"faction_b","SilverBloodFamily"},{"description","EMB-OLD"},{"when","yesterday"}},
        {{"type","embargo"},{"faction_a","SilverBloodFamily"},{"faction_b","ThalmorFaction"},{"description","EMB-MID"},{"when","earlier today"}},
        {{"type","espionage"},{"faction_a","ThalmorFaction"},{"faction_b","SilverBloodFamily"},{"description","SPY-ONE"},{"when","earlier today"}},
        {{"type","pe_visit"},{"faction_a",""},{"description","PE-SAME"}},{{"type","pe_visit"},{"faction_a",""},{"description","PE-SAME"}},
        {{"type","embargo"},{"faction_a","ThalmorFaction"},{"faction_b","SilverBloodFamily"},{"description","EMB-NEW"},{"when","just now"}}});
    auto merged=env.render(npc,data);
    Require(merged.find("EMB-OLD")==std::string::npos && merged.find("EMB-MID")==std::string::npos,"older repeats hidden");
    Require(merged.find("- (just now) EMB-NEW (reported 3 times)\n")!=std::string::npos,"newest repeat with count");
    Require(merged.find("SPY-ONE\n")!=std::string::npos && merged.find("SPY-ONE (reported")==std::string::npos,"single event has no count");
    Require(merged.find("PE-SAME")!=merged.rfind("PE-SAME"),"pe_* events stay unmerged");
    std::cout << merged << "\n";
    json dm={{"politicalContext",json{{"recent_events",json::array()}}.dump(2)},
        {"tickHours","6"},{"maxActiveWars","2"},{"maxRelationChange","15"},{"peActive",false}};
    const auto dmTemplate=Read("current1.prompt"), oldDm=Read("baseline1.prompt");
    auto inactive=env.render(dmTemplate,dm);
    Require(inactive==env.render(oldDm,dm) && inactive.find("Study the political state")!=std::string::npos,
            "inactive DM prompt must remain byte-identical");
    Require(dmTemplate.find("battle_result_allowed_now")!=std::string::npos && dmTemplate.find("battle_result_allowed_now")==dmTemplate.rfind("battle_result_allowed_now"),"DM prompt names the per-war battle flag once");
    dm["peActive"]=true;
    Require(env.render(dmTemplate,dm).find("political_reaction")!=std::string::npos &&
            env.render(dmTemplate,dm).find("reacts_to")!=std::string::npos, "active DM reaction rule");
    dm["politicalContext"]=json{{"relations",json::array({{{"faction_a","SilverBloodFamily"},{"faction_b","ThalmorFaction"},{"war_possible",false}},{{"faction_a","ImperialLegion"},{"faction_b","ThalmorFaction"},{"war_possible",true}}})}}.dump(2);
    { auto r=env.render(dmTemplate,dm); Require(r.find("\"war_possible\": false")!=std::string::npos && r.find("\"war_possible\": true")!=std::string::npos && dmTemplate.find("`war_possible: true`")!=std::string::npos,"war_possible renders in the DM state and the prompt names it"); }
    dm["politicalContext"]="";
    Require(env.render(dmTemplate,dm)==env.render(oldDm,dm), "empty context sentinel remains unchanged");
    std::cout << "PASS real Inja: inactive NPC prompt byte-identical; fix64 faction/visibility rules hold; fix68 repeats merge\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
''')
    subprocess.run(["cl","/nologo","/EHsc","/std:c++latest","/I"+str(pathlib.Path(sys.argv[1])/"include"),
                    "/I"+sys.argv[2],"check.cpp","/Fe:check.exe"],cwd=work,check=True)
    subprocess.run([str(work/"check.exe")],cwd=work,check=True)
