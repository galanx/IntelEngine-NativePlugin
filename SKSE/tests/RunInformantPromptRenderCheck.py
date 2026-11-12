"""Render the IE informant prompt sections (0800 facts, 0801 task, 7200 purpose) with real Inja, fed by values
the compiled Papyrus (bytecode VM) stores for an informant dispatch + arrival.
Usage (VS dev shell, PYTHONUTF8=1): python RunInformantPromptRenderCheck.py <papyrus.exe> <Game Scripts> <inja single_include> <nlohmann include>
"""
import importlib.util, json, pathlib, subprocess, sys, tempfile

papyrus, scripts, inja_inc, nlo_inc = sys.argv[1:5]
here = pathlib.Path(__file__).resolve().parent
prompts = here.parents[0] / "Plugins/SkyrimNet/external/galanx.intelengine/prompts/submodules/character_bio"
spec = importlib.util.spec_from_file_location("arrival", here / "RunArrivalDeliveryBytecodeCheck.py")
arrival = importlib.util.module_from_spec(spec); spec.loader.exec_module(arrival)
parse = lambda n: arrival.parse_functions(subprocess.run([papyrus, "read", str(pathlib.Path(scripts) / (n + ".pex"))],
                                                         check=True, capture_output=True, text=True).stdout)
core, story = parse("IntelEngine_Core"), parse("IntelEngine_StoryEngine")

class VM(arrival.DirectorVM):
    dispatched = None
    def invoke(self, script, name, args=()):
        if name == "DispatchToTarget":
            self.dispatched = args[2]; return True
        return super().invoke(script, name, args)
    def external(self, method, args, script):
        if method == "Find":
            return args[0].lower().find(args[1].lower(), args[2] if len(args) > 2 else 0)
        return super().external(method, args, script)

huntsman = "Gamlunda was seen slipping a folded note to a stranger by the Drunken Huntsman"
mare = "Sinmir was loudly criticizing Commander Caius and the Imperial guard at The Bannered Mare"
cases = {  # name: (informant, subject, rumor as the LLM wrote it, sender)
    "a) Marianus overheard, bad rumor": ("Marianus Cosma", "Gamlunda", "Marianus Cosma heard that " + huntsman + ".", ""),
    "b) Hulda told by Mikael": ("Hulda", "Sinmir", mare, "Mikael"),
    "c) sender == subject": ("Hulda", "Sinmir", "Sinmir bragged about the ale.", "Sinmir"),
}
data = {}
for label, (informant, subject, rumor, sender) in cases.items():
    vm = VM(({}, core, story), pathlib.Path("unused"))
    vm.target.name = vm.agent.name = informant
    resp = {"should_act": True, "type": "informant", "npc": informant, "narration": "x", "subject": subject,
            "rumor": rumor, "gossip": "", "sender": sender}
    vm.invoke("IntelEngine_StoryEngine", "OnDungeonMasterResponse", (json.dumps(resp), 1))
    fact = [e[1][1] for e in vm.effects if e[0] == "InjectFact"][0]
    line = vm.dispatched
    av = VM(({}, core, story), pathlib.Path("unused"))
    av.target.name = av.agent.name = informant
    av.properties.update({"IsActive": True, "ArrivalHandling": False, "ActiveStoryNPC": av.target,
                          "ActiveStoryType": "informant", "ActiveNarration": line, "DispatchGeneration": 1})
    av.invoke("IntelEngine_StoryEngine", "OnStoryNPCArrived")
    purpose = av.storage.get((av.target, "Intel_ArrivalPurpose"), "")
    # DispatchToTarget (StoryEngine.psc:1993) stores Intel_StoryNarration = narration; it is stubbed in the VM.
    data[label] = {"informant": informant, "fact": fact, "narration": line, "purpose": purpose,
                   "sent": [e[1][0] for e in av.effects if e[0] == "SendTaskNarration"]}

cpp = r'''
#include <inja/inja.hpp>
#include <fstream>
#include <iostream>
using nlohmann::json;
static std::string Read(const std::string& n){ std::ifstream f(n, std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
int main(int argc, char** argv) {
  inja::Environment env; env.set_line_statement(""); env.set_search_included_templates_in_files(false);
  auto in = json::parse(Read("cases.json"));
  for (auto& [label, c] : in.items()) {
    // IE C++ RenderFactsSection format as seen in openrouter_input.log: "## Things I Know\n- <fact> (just now)"
    json store = {{"Intel_FactsRendered", "## Things I Know\n- " + c["fact"].get<std::string>() + " (just now)\n"},
                  {"Intel_TaskHistoryRendered", ""}, {"Intel_TaskType","story"}, {"Intel_Target","Iepe"},
                  {"Intel_StoryNarration", c["narration"]}, {"Intel_ArrivalPurpose", c["purpose"]}};
    env.add_callback("papyrus_util", 4, [&](inja::Arguments& a){ auto k=a[2]->get<std::string>(); return store.contains(k)?store[k]:*a[3]; });
    env.add_callback("papyrus_util", 3, [&](inja::Arguments&){ return json::array(); });
    env.add_callback("isValidActor",1,[](inja::Arguments&){ return true; });
    env.add_callback("busy_reason",1,[](inja::Arguments&){ return std::string(); });
    env.add_callback("decnpc",1,[&](inja::Arguments&){ return json{{"name",c["informant"]}}; });
    env.add_callback("get_global_value",1,[](inja::Arguments&){ return 0.0; });
    json d = {{"render_mode","full"},{"actorUUID","x"},{"player",{{"name","Iepe"}}}};
    std::cout << "=========== CASE " << label << "\n";
    for (std::string f : {"0800_intel_facts.prompt","0801_intel_task_awareness.prompt","7200_intel_arrival_purpose.prompt"}) {
      std::cout << "----- " << f << "\n";
      std::string r = env.render(Read(argv[1] + f), d);
      std::string out; bool blank=false; for (char ch : r) { if (ch=='\n' && !out.empty() && out.back()=='\n') { if(blank) continue; blank=true; } else blank=false; out+=ch; }
      std::cout << out << "\n";
    }
    std::cout << "----- SkyrimNet-owned, built from IE's DirectNarration text\n"
              << "This event just happened and must be responded to, no matter what: `" << c["sent"][0].get<std::string>() << "`. This event is completely factual.\n";
  }
}
'''
with tempfile.TemporaryDirectory(prefix="inf-render-") as d:
    w = pathlib.Path(d)
    (w / "cases.json").write_text(json.dumps(data), encoding="utf-8")
    (w / "check.cpp").write_text(cpp, encoding="utf-8")
    subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/I" + inja_inc, "/I" + nlo_inc, "check.cpp", "/Fe:check.exe"], cwd=w, check=True, stdout=subprocess.DEVNULL)
    out = subprocess.run([str(w / "check.exe"), str(prompts) + "/"], cwd=w, check=True, capture_output=True, text=True, encoding="utf-8").stdout
    print(out)
    print("rumor-bearing sentence counts (IE-owned: 0800+0801+7200+DirectNarration line):")
    for label, c in data.items():
        core_text = c["fact"].split(" that ", 1)[-1] if " that " in c["fact"] else c["fact"]
        sect = out.split("=========== CASE ")[[k for k in data].index(label) + 1]
        print(" ", label, "->", sect.count(core_text))
