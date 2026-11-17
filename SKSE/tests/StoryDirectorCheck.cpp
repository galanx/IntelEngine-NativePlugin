// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/StoryDirectorCheck.cpp
#include "StoryDirectorPick.h"

#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace IntelEngine::StoryDirector;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Verdict V(const char* name, int level, float expected, const char* kind, float rank = 1.0f,
                 const char* allowed = "seek_player, informant, message, quest") {
    Verdict v;
    v.name = name;
    v.answered = true;
    v.level = level;
    v.expected = expected;
    v.kind = kind;
    v.rank = rank;
    v.allowed = allowed;
    return v;
}

static void Levels() {
    Require(LevelOf("none") == 0 && LevelOf("real") == 3 && LevelOf("urgent") == 5 && LevelOf("x") == -1, "the levels");
    Require(ExpectedLevel({{"real", 0.5f}, {"pressing", 0.5f}}, "real") == 3.5f, "weighted by probability");
    Require(ExpectedLevel({}, "pressing") == 4.0f, "the chosen level alone without probabilities");
    Require(ExpectedLevel({{"bogus", 1.0f}}, "mild") == 2.0f, "unknown keys are ignored");
    Require(ExpectedLevel({{"none", 0.0f}}, "x") == 0.0f, "nothing usable is none");
}

static void Picks() {
    const auto never = [](const std::string&) { return 0; };
    Require(Pick({}, never) == -1, "nobody, no moment");
    Require(Pick({V("a", 2, 2.0f, "seek_player"), V("b", 1, 1.0f, "message")}, never) == -1,
            "only small reasons: no moment");
    Require(Pick({V("a", 3, 2.2f, "seek_player")}, never) == -1, "a real reason the probabilities doubt is not enough");
    Require(Pick({V("a", 3, 3.0f, "none")}, never) == -1, "a reason but no kind of moment");
    Require(Pick({V("a", 4, 4.0f, "ambush", 1.0f, "seek_player, message")}, never) == -1, "a kind they may not be used for");
    Verdict unanswered = V("a", 5, 5.0f, "seek_player");
    unanswered.answered = false;
    Require(Pick({unanswered}, never) == -1, "no answer, no pick");

    Require(Pick({V("a", 3, 3.0f, "message"), V("b", 5, 4.8f, "seek_player")}, never) == 1, "the strongest reason acts");

    std::map<std::string, int> counts{{"seek_player", 5}, {"message", 0}};
    const auto picked = [&](const std::string& kind) { return counts[kind]; };
    Require(Pick({V("a", 4, 4.0f, "seek_player", 9.0f), V("b", 4, 3.7f, "message", 1.0f)}, picked) == 1,
            "a close call goes to the fresher kind of moment");
    Require(Pick({V("a", 4, 4.0f, "seek_player", 1.0f), V("b", 4, 3.8f, "seek_player", 9.0f)}, picked) == 1,
            "same kind: the code's own rank");
    Require(Pick({V("a", 5, 4.9f, "seek_player"), V("b", 3, 3.0f, "message")}, picked) == 0,
            "freshness never beats a clearly stronger reason");
    Require(Allowed(V("a", 3, 3.0f, "quest", 1.0f, "quest")), "a single allowed kind");
    Require(!Allowed(V("a", 3, 3.0f, "quest", 1.0f, "quest_combat")), "no partial match");
}

int main() {
    try {
        Levels();
        Picks();
    } catch (const std::exception& e) {
        std::cerr << "StoryDirectorCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "StoryDirectorCheck passed\n";
    return 0;
}
