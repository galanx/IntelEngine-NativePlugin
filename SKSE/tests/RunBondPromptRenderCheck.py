"""Render the two prompts that read how somebody stands with the player (decisions/intel_bond.prompt and its chat form
intel_bond_chat.prompt, shipped by the GamePlugin) with the Inja that SkyrimNet itself builds with, and check that
every answer name Bonds.h reads is offered in both.

A prompt that fails to render in the game costs the reading silently, and the answer names are a contract: a name the
prompt offers and Bonds::Read does not know is a reading thrown away; one Bonds::Read knows and the prompt does not
offer can never be read.

Usage (VS developer shell, PYTHONUTF8=1):
    python RunBondPromptRenderCheck.py <intel_bond.prompt> <intel_bond_chat.prompt> <Bonds.h> <inja single_include> <nlohmann include>
"""
import json
import pathlib
import re
import subprocess
import sys
import tempfile

decision, chat, bonds_h, inja_inc, nlo_inc = sys.argv[1:6]

CASES = {
    "memories and talk": {
        "npc_name": "Ysolda", "player_name": "Ada",
        "memories": "- [experience, warm, 2 days ago] Ada brought me the mammoth tusk I asked for and would take no coin.\n"
                    "- [experience, shy, 1 day ago] I told Ada I think of her more than I should.",
        "talk": "Ada: You look well today.\nYsolda: Better for seeing you, if I may say so.",
    },
    "nothing on record but talk": {"npc_name": "Lod", "player_name": "Ada", "memories": "(none on record)", "talk": "Lod: Need something forged?"},
    "a name with braces and quotes": {"npc_name": "J'zargo {the \"great\"}", "player_name": "Ada", "memories": "- {odd} \"text\"", "talk": "(none on record)"},
}

# The names Bonds::Read compares with, read from the header itself.
header = pathlib.Path(bonds_h).read_text(encoding="utf-8")
read = header[header.index("inline Reading Read("):header.index("// Close enough to count as bonded")]
NAMES = sorted(set(re.findall(r'== "([a-z]+)"', read)))
EXPECTED = {"stranger", "acquaintance", "friend", "confidant", "lover", "cold", "foe", "drawn", "welcomed", "declined"}
if set(NAMES) != EXPECTED:
    sys.exit("FAIL: the names Bonds::Read compares with are %s, this check expects %s" % (NAMES, sorted(EXPECTED)))
OFFERED = sorted(EXPECTED | {"none", "unanswered"})   # the two answers that mean "nothing": offered, read as anything else

cpp = r'''
#include <inja/inja.hpp>
#include <fstream>
#include <iostream>
using nlohmann::json;
static std::string Read(const std::string& n){ std::ifstream f(n, std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
int main(int argc, char** argv) {
  inja::Environment env; env.set_line_statement(""); env.set_search_included_templates_in_files(false);
  auto in = json::parse(Read("cases.json"));
  json out = json::object();
  for (int i = 1; i < argc; ++i) {
    const std::string text = Read(argv[i]);
    for (auto& [label, c] : in.items()) {
      const std::string key = std::to_string(i) + ": " + label;
      try { out[key] = env.render(text, c); }
      catch (const std::exception& e) { out[key] = std::string("RENDER ERROR: ") + e.what(); }
    }
  }
  std::ofstream("rendered.json", std::ios::binary) << out.dump();
}
'''

with tempfile.TemporaryDirectory(prefix="bond-prompt-") as d:
    w = pathlib.Path(d)
    (w / "cases.json").write_text(json.dumps(CASES), encoding="utf-8")
    (w / "check.cpp").write_text(cpp, encoding="utf-8")
    subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/I" + inja_inc, "/I" + nlo_inc, "check.cpp", "/Fe:check.exe"],
                   cwd=w, check=True, capture_output=True)
    subprocess.run([str(w / "check.exe"), decision, chat], cwd=w, check=True)
    rendered = json.loads((w / "rendered.json").read_text(encoding="utf-8"))

failed = 0
for key, text in rendered.items():
    which, label = key.split(": ", 1)
    form = "decision" if which == "1" else "chat"
    case = CASES[label]
    problems = []
    if text.startswith("RENDER ERROR"):
        problems.append(text[:300])
    else:
        for name in OFFERED:
            # Offered as a choice: "name:" at the start of a criteria line, "- name:" or ", name" in the chat form's lists.
            if not re.search(r"(^|[\s,(-])%s\b" % name, text, re.M):
                problems.append("the answer '%s' is not offered" % name)
        for must in (case["npc_name"], case["player_name"], case["memories"].splitlines()[0], case["talk"].splitlines()[0]):
            if must not in text:
                problems.append("missing: " + must[:60])
        if "{{" in text or "{%" in text:
            problems.append("a template tag was left unrendered")
        if form == "decision":
            for must in ("[ question standing choice ]", "[ question heart choice ]", "[ question answer choice ]", "[ end criteria ]"):
                if must not in text:
                    problems.append("missing: " + must)
        else:
            if '{"standing":"friend","heart":"none","answer":"unanswered"}' not in text:
                problems.append("the answer's shape is not shown")
    if problems:
        failed += 1
        print("FAIL  %s form, %s: %s" % (form, label, "; ".join(problems)))
if failed:
    sys.exit(1)
print("bond prompts render: %d renderings, every answer name of Bonds.h offered in both forms" % len(rendered))
