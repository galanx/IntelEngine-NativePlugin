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

    // A quiet world: a mild reason may bring a gentle moment, never a quest or an ambush, and a real one still wins.
    Require(Pick({V("a", 2, 1.9f, "informant")}, never, true) == 0, "quiet: a mild reason brings a gentle moment");
    Require(Pick({V("a", 2, 1.9f, "informant")}, never, false) == -1, "not quiet: a mild reason is not enough");
    Require(Pick({V("a", 2, 2.0f, "quest"), V("b", 2, 2.0f, "ambush")}, never, true) == -1,
            "quiet: never a quest or an ambush on a mild reason");
    Require(Pick({V("a", 2, 1.2f, "seek_player")}, never, true) == -1, "quiet: the probabilities must lean to mild");
    Require(Pick({V("a", 2, 2.0f, "message"), V("b", 4, 3.9f, "seek_player")}, never, true) == 1,
            "quiet: a real reason still wins over a mild one");

    // Nobody twice running: someone who acted lately needs a pressing reason to act again.
    Verdict again = V("a", 3, 3.0f, "seek_player");
    again.sincePicked = 0.5f;
    Require(Pick({again}, never) == -1, "a real reason again within two days: no");
    again.level = 4;
    again.expected = 4.0f;
    Require(Pick({again}, never) == 0, "a pressing reason may come back sooner");
    Verdict other = V("b", 3, 2.8f, "message");
    Verdict repeat = V("a", 3, 3.4f, "seek_player");
    repeat.sincePicked = 1.0f;
    Require(Pick({repeat, other}, never) == 1, "someone else with a real reason acts instead");
    repeat.sincePicked = 3.0f;
    Require(Pick({repeat, other}, never) == 0, "after two days they may act again");
}

static Verdict At(Verdict v, int reach, bool bonded = false, bool farRested = true) {
    v.reach = reach;
    v.bonded = bonded;
    v.farRested = farRested;
    return v;
}

// How far a reason carries: the user's complaint was people visiting from afar for absurd reasons.
static void Distance() {
    const auto never = [](const std::string&) { return 0; };
    const char* all = "seek_player, informant, message, quest, ambush, stalker, road_encounter";
    // Here: as before.
    Require(Pick({At(V("a", 3, 3.0f, "seek_player"), 0)}, never) == 0, "someone here acts on a real reason");
    // Elsewhere in the hold: a real reason only from someone close; anyone on a pressing one.
    Require(Pick({At(V("a", 3, 3.0f, "quest"), 1)}, never) == -1, "an acquaintance does not walk hours for a matter that can wait");
    Require(Pick({At(V("a", 3, 3.0f, "quest"), 1, true)}, never) == 0, "someone close does");
    Require(Pick({At(V("a", 4, 4.0f, "quest"), 1)}, never) == 0, "a pressing matter brings anyone of the hold");
    Require(Pick({At(V("a", 3, 3.0f, "ambush", 1.0f, all), 1)}, never) == 0, "the hold's roads are as before");
    // Another hold.
    Require(Pick({At(V("a", 4, 4.0f, "quest"), 2)}, never) == -1, "nobody crosses Skyrim on a pressing matter for an acquaintance");
    Require(Pick({At(V("a", 5, 4.2f, "quest"), 2)}, never) == -1, "nor on an urgent one the probabilities doubt");
    Require(Pick({At(V("a", 5, 4.8f, "quest"), 2)}, never) == 0, "urgent beyond doubt: they come");
    Require(Pick({At(V("a", 3, 3.0f, "seek_player"), 2, true)}, never) == -1, "even someone close does not cross Skyrim for a matter that can wait");
    Require(Pick({At(V("a", 4, 4.0f, "seek_player"), 2, true)}, never) == 0, "someone close, on a pressing matter: they come");
    Require(Pick({At(V("a", 5, 5.0f, "quest"), 2, true, false)}, never) == -1, "not twice in a short while, whatever the reason");
    Require(Pick({At(V("a", 5, 5.0f, "ambush", 1.0f, all), 2, true)}, never) == -1, "nobody crosses Skyrim to lie in wait");
    Require(Pick({At(V("a", 5, 5.0f, "stalker", 1.0f, all), 2)}, never) == -1, "nor to shadow the player");
    Require(Pick({At(V("a", 3, 3.0f, "road_encounter", 1.0f, all), 2)}, never) == -1, "a stranger from afar is not met on the road by chance");
    Require(Pick({At(V("a", 3, 3.0f, "road_encounter", 1.0f, all), 2, true)}, never) == 0, "an old companion may be");
    // The far one is passed over and someone here with a real reason acts instead.
    Require(Pick({At(V("far", 4, 4.0f, "quest"), 2), At(V("here", 3, 3.0f, "seek_player"), 0)}, never) == 1, "the one who is here acts");
    // A quiet world's gentle moment on a mild reason: only from someone who is here anyway.
    Require(Pick({At(V("a", 2, 2.0f, "seek_player"), 0)}, never, true) == 0, "a mild reason, here, in a quiet world");
    Require(Pick({At(V("a", 2, 2.0f, "seek_player"), 1, true)}, never, true) == -1, "never from down the road");
}

int main() {
    try {
        Levels();
        Picks();
        Distance();
    } catch (const std::exception& e) {
        std::cerr << "StoryDirectorCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "StoryDirectorCheck passed\n";
    return 0;
}
