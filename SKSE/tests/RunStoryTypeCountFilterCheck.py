"""Compile real count formatting without the game ABI; requires clang++.

Usage: python SKSE/tests/RunStoryTypeCountFilterCheck.py
"""
import pathlib
import subprocess
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / "src"
source = (src / "NPCIndex.cpp").read_text(encoding="utf-8")
start = source.index("    std::string NPCIndex::GetStoryTypeCountsMarkdown(")
end = source.index("\n    }", start) + len("\n    }")
with tempfile.TemporaryDirectory(prefix="story-counts-") as directory:
    work = pathlib.Path(directory)
    cpp = work / "check.cpp"
    cpp.write_text('''#include <cassert>
#include <iostream>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
class NPCIndex {
public:
    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, int> m_storyTypeCounts;
    std::string GetStoryTypeCountsMarkdown(const std::unordered_set<std::string>&,
                                         const std::unordered_set<std::string>&) const;
};
''' + source[start:end] + '''
int main() {
    NPCIndex index;
    index.m_storyTypeCounts = {{"seek_player", 3}, {"quest", 2}, {"ambush", 1}, {"npc_gossip", 4}};
    const std::unordered_set<std::string> dm = {"seek_player", "quest", "ambush"};
    auto counts = index.GetStoryTypeCountsMarkdown(dm, {"seek_player", "ambush"});
    assert(counts == "quest: 2");
    counts = index.GetStoryTypeCountsMarkdown(dm, {});
    assert(counts.find("seek_player: 3") != std::string::npos);
    assert(counts.find("quest: 2") != std::string::npos);
    assert(counts.find("ambush: 1") != std::string::npos);
    assert(counts.find("npc_gossip") == std::string::npos);
    assert(index.GetStoryTypeCountsMarkdown(dm, dm).empty());
    assert(index.GetStoryTypeCountsMarkdown({"quest"}, {"quest_combat"}) == "quest: 2");
    assert(index.GetStoryTypeCountsMarkdown({"npc_gossip"}, {}) == "npc_gossip: 4");
    std::cout << "PASS: real count formatting omits exclusions; empty/all exclusions, exact tokens, NPC counts\\n";
}
''', encoding="utf-8")
    exe = work / "check.exe"
    subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
