"""Render the tale author's prompt (intel_tale_author.prompt, shipped by the GamePlugin) with the Inja that SkyrimNet
itself builds with, for the kinds of request TaleAuthor.cpp makes, and check what the author is and is not told.

A prompt that fails to render in the game costs a story silently (the request comes back empty and the old patterns
are told instead), and Inja's errors are easy to write: a line that starts with "{" and is no statement, a missing
endif. Stories of the heart add branches that are only taken for a few people, so they are rendered here.

Usage (VS developer shell, PYTHONUTF8=1):
    python RunTaleAuthorPromptRenderCheck.py <intel_tale_author.prompt> <inja single_include> <nlohmann include>
"""
import json
import pathlib
import subprocess
import sys
import tempfile

prompt, inja_inc, nlo_inc = sys.argv[1:4]

PEOPLE = [
    {"key": "p0", "name": "Ysolda", "sex": "woman", "race": "Nord", "work": "trades at the market", "temperament": "law-abiding, careful",
     "home": "a home of her own", "may": [], "ties": ["Mallus: friend"], "about": "Wants to buy the Bannered Mare one day."},
    {"key": "p1", "name": "Jon Battle-Born", "sex": "man", "race": "Nord", "work": "a bard at heart", "temperament": "bold",
     "home": "House of Clan Battle-Born", "may": ["would settle a matter with fists"], "ties": ["Olfina Gray-Mane: their lover (lover)"], "close": True,
     "quest": "The Whispering Door (not begun)", "about": "Writes verse in secret."},
    {"key": "p2", "name": "Hulda", "sex": "woman", "race": "Nord", "work": "keeps the Bannered Mare", "temperament": "steady",
     "home": "the Bannered Mare", "may": [], "ties": [], "favours": "A favour for Hulda (finished)"},
]
BASE = {
    "player_name": "Ada", "player_sex": "woman", "player_race": "Breton", "player_level": 12, "town": "Whiterun", "hold": "Whiterun Hold",
    "asker": "p0", "asker_name": "Ysolda", "people": PEOPLE, "items": ["Silver Ring", "Lute (home only)"],
    "places": [{"key": "d1", "name": "Halted Stream Camp", "what": "a bandit camp", "heart": "a bandit chief", "far": "close by", "cleared": False, "holds": True},
               {"key": "d2", "name": "Shimmermist Cave", "what": "a Falmer hive and a Dwemer hall", "heart": "a Dwarven centurion", "far": "half a day's walk", "cleared": True, "holds": False}],
    "quests": ["Dragon Rising: Kill the dragon"], "told": ["crime: The Quiet Ledger - a ledger that lied"], "lean": "debt", "reward": 150,
    "bar_ambush": False, "bar_brawl": True, "bar_stalk": False, "bar_places": False, "asker_knows": "met",
    "must_place": False, "must_captive": False, "bar_captive": False,
}


def case(**changes):
    c = json.loads(json.dumps(BASE))
    for key, value in changes.items():
        if key == "heart":                      # the asker's own line
            c["people"][0]["heart"] = value
        else:
            c[key] = value
    return c


HEART = "# Between Ysolda and Ada"   # the section's heading (a rule further down names the section in quotes)
TAKEN = "# Someone has been taken"
CASES = {
    "an ordinary story": (case(), {"has": ["# The people you may use", "Lean toward debt", "Jon Battle-Born", "Halted Stream Camp, a bandit camp, close by; at its heart: a bandit chief. Someone may be held here.",
                                           '"free": someone of the story is held at a place', "one act of a story, never the whole of it", "Never an errand"],
                                   "not": [HEART, "With Ada:", TAKEN, "This story leads out into the world", "a Dwarven centurion. Cleared once already; something new has moved in. Someone may be held here."]}),
    "out into the world": (case(must_place=True), {"has": ["This story leads out into the world", "one act of a story"], "not": [HEART, TAKEN]}),
    "a rescue": (case(must_captive=True, lean="adventure"), {"has": [TAKEN, "who is held at one of the places below", '"do": "free"', "Close to Ysolda.", "close to Ysolda (nobody asks a stranger"], "not": [HEART]}),
    "nobody held this time": (case(bar_captive=True), {"has": ['"delve":'], "not": ['"free": someone of the story', TAKEN]}),
    "staying in town": (case(bar_places=True, bar_captive=True), {"has": ["This story stays among people"], "not": ['"delve":', '"free": someone of the story', "one act of a story", TAKEN]}),
    "a stranger asks": (case(asker_knows="stranger"), {"has": ["a stranger taking a chance on a stranger"], "not": [HEART]}),
    "courtship": (case(lean="courtship", heart_lean="courtship", heart="To Ada: a close confidant, and there is more than friendship in it. Ada has not yet answered what they feel."),
                  {"has": [HEART, "Something has begun between them", "With Ada: To Ada: a close confidant", '"effect": "closer p0"',
                           "can always say no kindly", "the story ends at the door", "Only grown people"],
                   "not": ["They are sure of each other", "a story of the household", "is married to another"]}),
    "a life shared": (case(lean="a life shared", heart_lean="together", heart="Married to Ada."),
                      {"has": [HEART, "They are sure of each other", "With Ada: Married to Ada."],
                       "not": ["Something has begun between them", "a story of the household", "is married to another"]}),
    "the household": (case(lean="the household", heart_lean="family", heart="Married to Ada. Has a child with Ada."),
                      {"has": [HEART, "a story of the household", "Children are spoken of and never given a part"],
                       "not": ["Something has begun between them", "They are sure of each other"]}),
    "an affair": (case(lean="an affair", heart_lean="affair", heart="To Ada: a lover. Married to someone else."),
                  {"has": [HEART, "is married to another", "never make them a fool or a villain"],
                   "not": ["Something has begun between them", "a story of the household"]}),
    # Someone else in town has a heart for the player, but the asker has not: the fact is told, no such story is asked for.
    "a spouse in the cast": (dict(case(), people=[PEOPLE[0], dict(PEOPLE[1], heart="Married to Ada.")] + PEOPLE[2:]),
                             {"has": ["With Ada: Married to Ada.", "note is fact"], "not": [HEART]}),
    "a reason of their own": (case(why="Ysolda owes Ada for the mammoth tusk.", kind="visit"), {"has": ["# Why this story begins now", "mammoth tusk"], "not": [HEART]}),
    "a second attempt": (case(fix="step s2: no way from it leads to an ending", previous="{}"), {"has": ["no way from it leads to an ending"], "not": [HEART]}),
}

cpp = r'''
#include <inja/inja.hpp>
#include <fstream>
#include <iostream>
using nlohmann::json;
static std::string Read(const std::string& n){ std::ifstream f(n, std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
int main(int, char** argv) {
  inja::Environment env; env.set_line_statement(""); env.set_search_included_templates_in_files(false);
  const std::string text = Read(argv[1]);
  auto in = json::parse(Read("cases.json"));
  json out = json::object();
  for (auto& [label, c] : in.items()) {
    try { out[label] = env.render(text, c); }
    catch (const std::exception& e) { out[label] = std::string("RENDER ERROR: ") + e.what(); }
  }
  std::ofstream("rendered.json", std::ios::binary) << out.dump();
}
'''

with tempfile.TemporaryDirectory(prefix="tale-prompt-") as d:
    w = pathlib.Path(d)
    (w / "cases.json").write_text(json.dumps({k: v[0] for k, v in CASES.items()}), encoding="utf-8")
    (w / "check.cpp").write_text(cpp, encoding="utf-8")
    subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++17", "/utf-8", "/I" + inja_inc, "/I" + nlo_inc, "check.cpp", "/Fe:check.exe"],
                   cwd=w, check=True, capture_output=True)
    subprocess.run([str(w / "check.exe"), prompt], cwd=w, check=True)
    rendered = json.loads((w / "rendered.json").read_text(encoding="utf-8"))

failed = 0
for label, (_ctx, expect) in CASES.items():
    text = rendered[label]
    problems = []
    if text.startswith("RENDER ERROR"):
        problems.append(text[:300])
    else:
        problems += ["missing: " + s for s in expect["has"] if s not in text]
        problems += ["must not be there: " + s for s in expect["not"] if s in text]
        problems += ["unrendered template text: " + s for s in ("{{", "{%", "%}") if s in text]
        if "[ system ]" not in text or "[ user ]" not in text:
            problems.append("the system and user parts are not both there")
    print(("FAIL " if problems else "ok   ") + label + (": " + "; ".join(problems) if problems else " (%d characters)" % len(text)))
    failed += bool(problems)
if failed:
    sys.exit(1)
print("tale author prompt: %d requests render as meant" % len(CASES))
