"""Exercise production shared-knowledge extraction and render the full Jinja prompt.

Usage: python SKSE/tests/StorySharedKnowledgeRenderCheck.py
Requires clang++ and Jinja2; installs are intentionally not managed here.
"""
import pathlib
import subprocess
import tempfile

from jinja2 import ChainableUndefined, Environment

repo = pathlib.Path(__file__).resolve().parents[1]
source = (repo / "src/NPCIndex.cpp").read_text(encoding="utf-8")
start = source.index("    static std::vector<std::string> ExtractSharedKnowledge(")
end = source.index("\n    }\n", start) + len("\n    }")
helper = source[start:end]

with tempfile.TemporaryDirectory(prefix="story-shared-knowledge-") as directory:
    work = pathlib.Path(directory)
    cpp = work / "check.cpp"
    cpp.write_text(r'''#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
''' + helper + r'''
using Knowledge = std::vector<std::vector<std::string>>;

std::string render(Knowledge knowledge, const std::vector<std::string>& names) {
    const auto shared = ExtractSharedKnowledge(knowledge);
    std::string output;
    if (!shared.empty()) {
        output += "## Everyone here knows:\n";
        for (const auto& fact : shared) output += "- " + fact + "\n";
        output += "\n";
    }
    output += "## Candidate Pool\n\n";
    for (size_t i = 0; i < knowledge.size(); ++i) {
        output += "### " + std::to_string(i + 1) + ". " + names[i] + " [Civilian, Female] - Whiterun\n";
        output += "Hold: Whiterun Hold\nDistance: nearby\nKnows player: acquainted\nLast met player: 2 days ago\n";
        if (!knowledge[i].empty()) {
            output += "Knows:\n";
            for (const auto& fact : knowledge[i]) output += "- " + fact + "\n";
        }
        output += "\n";
    }
    return output;
}

int main() {
    Knowledge none;
    assert(ExtractSharedKnowledge(none).empty());
    Knowledge one{{"one candidate fact"}};
    assert(ExtractSharedKnowledge(one).empty() && one[0] == std::vector<std::string>{"one candidate fact"});
    Knowledge noShared{{"A"}, {"B"}};
    assert(ExtractSharedKnowledge(noShared).empty() && noShared[0][0] == "A" && noShared[1][0] == "B");
    Knowledge allShared{{"A", "B"}, {"A", "B"}};
    assert(ExtractSharedKnowledge(allShared) == std::vector<std::string>({"A", "B"}));
    assert(allShared[0].empty() && allShared[1].empty());
    Knowledge emptyCandidate{{"A"}, {}};
    assert(ExtractSharedKnowledge(emptyCandidate).empty() && emptyCandidate[0][0] == "A");
    Knowledge knowsLess{{"A", "B"}, {"A"}};
    assert(ExtractSharedKnowledge(knowsLess) == std::vector<std::string>{"A"});
    assert(knowsLess[0] == std::vector<std::string>{"B"} && knowsLess[1].empty());

    const std::string civilWar = "The civil war remains unresolved, and both sides continue to recruit soldiers across Skyrim.";
    const std::string concordat = "The White-Gold Concordat outlawed Talos worship in the Empire, though many people still practice in secret.";
    Knowledge realistic{
        {civilWar, concordat, "The Bannered Mare in Whiterun is a popular gathering place for travelers and locals."},
        {civilWar, concordat, "The Silver-Blood family controls much of Markarth's mining industry."},
        {civilWar, concordat, "Alduin was defeated at the Throat of the World, ending the immediate dragon threat."},
        {civilWar, concordat, "The College of Winterhold accepts students regardless of prior magical training."},
    };
    const std::vector<std::string> names{"Lydia", "Ysolda", "Adrianne", "Carlotta Valerius"};
    std::string before = "## Candidate Pool\n\n";
    for (size_t i = 0; i < realistic.size(); ++i) {
        before += "### " + std::to_string(i + 1) + ". " + names[i] + " [Civilian, Female] - Whiterun\n";
        before += "Hold: Whiterun Hold\nDistance: nearby\nKnows player: acquainted\nLast met player: 2 days ago\nKnows:\n";
        for (const auto& fact : realistic[i]) before += "- " + fact + "\n";
        before += "\n";
    }
    std::cout << before << "\n===== AFTER =====\n" << render(realistic, names);
}
''', encoding="utf-8")
    exe = work / "check.exe"
    subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    rendered = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout

before_pool, after_pool = rendered.split("\n===== AFTER =====\n", 1)
template_path = repo / "Plugins/SkyrimNet/external/galanx.intelengine/prompts/intel_story_dm.prompt"
env = Environment(undefined=ChainableUndefined, keep_trailing_newline=True)
template = env.from_string(template_path.read_text(encoding="utf-8"))
context = {
    "player": {"name": "The Dragonborn"},
    "candidatePool": "",
    "show_seek_player": "1", "show_informant": "1", "show_road_encounter": "1",
    "show_ambush": "1", "show_faction_ambush": "1", "show_stalker": "1", "show_message": "1",
    "show_quest": "1", "show_quest_combat": "1", "show_quest_rescue": "1",
    "show_quest_find_item": "1", "show_quest_faction_combat": "1",
    "show_quest_faction_rescue": "1", "show_quest_faction_battle": "1",
    "hostile_factions": "ThalmorFaction, ForswornFaction",
    "friendly_factions": "ImperialFaction, StormcloakFaction",
    "player_at_inn": "1", "latest_witness_event": "A dragon attacked Whiterun yesterday.",
    "world_quiet": "0",
}
context["candidatePool"] = before_pool
before = template.render(context)
context["candidatePool"] = after_pool
rendered_after = template.render(context)
shared_facts = [
    "The civil war remains unresolved, and both sides continue to recruit soldiers across Skyrim.",
    "The White-Gold Concordat outlawed Talos worship in the Empire, though many people still practice in secret.",
]
assert all(rendered_after.count(fact) == 1 for fact in shared_facts)
assert all(rendered_after.count(fact) == 1 for fact in [
    "The Bannered Mare in Whiterun is a popular gathering place for travelers and locals.",
    "The Silver-Blood family controls much of Markarth's mining industry.",
    "Alduin was defeated at the Throat of the World, ending the immediate dragon threat.",
    "The College of Winterhold accepts students regardless of prior magical training.",
])
assert len(rendered_after) < len(before)
print(f"Before full rendered prompt characters: {len(before)}")
print(f"After full rendered prompt characters:  {len(rendered_after)}")
print(f"Reduced by: {len(before) - len(rendered_after)} characters")
print("PASS: extracted production logic preserves all/partial/none/empty/0/1-candidate cases and renders shared knowledge once")
