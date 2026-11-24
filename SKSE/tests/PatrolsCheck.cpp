// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/PatrolsCheck.cpp
#include "Patrols.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::Patrols;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        // Who: only a faction that holds the player for an enemy (the same -40 the story engine checks).
        Require(!Due(0, 10.0f, -1.0f, 0) && !Due(-39, 10.0f, -1.0f, 0), "a faction on speaking terms sends nobody, whatever the roll");
        Require(Due(-40, 10.0f, -1.0f, 0), "at -40 it may");
        // When: some of the moments that may land, more of them the deeper the feud.
        Require(Chance(-39) == 0.0f && Chance(-40) == 0.2f && Chance(-60) == 0.35f && Chance(-80) == 0.5f && Chance(-100) == 0.5f,
                "the share of moments by standing");
        Require(Due(-40, 10.0f, -1.0f, 1999) && !Due(-40, 10.0f, -1.0f, 2000), "one moment in five while the feud is young");
        Require(Due(-90, 10.0f, -1.0f, 4999) && !Due(-90, 10.0f, -1.0f, 5000), "every other one when it is deep");
        int came = 0;
        for (std::uint32_t roll = 0; roll < 10000; ++roll) came += Due(-70, 10.0f, -1.0f, roll) ? 1 : 0;
        Require(came == 3500, "35 in a hundred at -70");
        // Not one patrol on the heels of another.
        Require(!Due(-90, 10.0f, 9.0f, 0) && !Due(-90, 10.0f, 8.01f, 0), "not within two days of the last one");
        Require(Due(-90, 10.0f, 8.0f, 0), "after two days again");
        Require(Due(-90, 3.0f, 9.0f, 0), "the clock was set back: the last one is not held against them");
        // How many: as the 3.8 handler held it (no more than four above -60, three to seven in all).
        for (std::uint32_t seed = 0; seed < 12; ++seed) {
            Require(Size(-40, seed) >= 3 && Size(-40, seed) <= 4 && Size(-59, seed) <= 4, "three or four while the feud is young");
            Require(Size(-60, seed) >= 4 && Size(-79, seed) <= 5, "four or five as it deepens");
            Require(Size(-80, seed) >= 5 && Size(-100, seed) <= 7, "five to seven when it is deep");
        }
        // Private moments: nobody in arms comes upon somebody who is with somebody or has settled somewhere.
        Privacy road;
        road.tracked = true;
        road.moved = 4200.0f;
        road.sinceTalk = 900.0f;
        road.sinceScene = 4000.0f;
        Require(PrivateMoment(road) == Private::No, "a traveller on the road, long after any talk or scene: they may be come upon");
        Privacy p = road;
        p.scene = true;
        Require(PrivateMoment(p) == Private::Scene, "never during a scene");
        p = road;
        p.sinceScene = 599.0f;
        Require(PrivateMoment(p) == Private::AfterScene, "nor within ten minutes of one");
        p.sinceScene = 600.0f;
        Require(PrivateMoment(p) == Private::No, "after that again");
        p = road;
        p.sinceTalk = 30.0f;
        Require(PrivateMoment(p) == Private::Talk, "not while somebody is talking with the player");
        p.sinceTalk = 121.0f;
        Require(PrivateMoment(p) == Private::No, "two minutes after the last word again");
        p = road;
        p.resting = true;
        Require(PrivateMoment(p) == Private::Resting, "not upon somebody who sits or sleeps");
        p = road;
        p.undressed = true;
        Require(PrivateMoment(p) == Private::Undressed, "not upon somebody who wears nothing");
        p = road;
        p.moved = 300.0f;
        Require(PrivateMoment(p) == Private::Settled, "not upon somebody who has stayed where they are");
        p.crossed = true;
        Require(PrivateMoment(p) == Private::No, "going through a door is travelling");
        p = road;
        p.moved = -1.0f;
        Require(PrivateMoment(p) == Private::JustArrived, "right after a load nothing is known: nobody comes");
        Privacy untracked;
        Require(PrivateMoment(untracked) == Private::No, "where nothing follows the player's steps, standing still is not held against them");
        untracked.sinceTalk = 5.0f;
        Require(PrivateMoment(untracked) == Private::Talk, "the rest holds all the same");
        Privacy never = road;
        never.sinceTalk = -1.0f;
        never.sinceScene = -1.0f;
        Require(PrivateMoment(never) == Private::No, "no talk and no scene this session: nothing to wait out");
        Require(Word(Private::Talk)[0] != 0 && Word(Private::No)[0] == 0, "every reason has its word");
        // Places nobody is sent to.
        Place town;
        Require(PlaceBars(town) == Barred::No, "a town of Skyrim: people come");
        Place soulCairn;
        soulCairn.inAHold = false;
        soulCairn.landWalkable = false;
        Require(PlaceBars(soulCairn) == Barred::Beyond, "the Soul Cairn: no hold, no land one can walk to: nobody");
        soulCairn.classic = true;
        Require(PlaceBars(soulCairn) == Barred::Beyond, "at Classic too: there is no road there");
        Place keep = soulCairn;       // a building inside it
        keep.outdoors = false;
        keep.lastLandKnown = true;
        keep.lastLandWalkable = false;
        Require(PlaceBars(keep) == Barred::Beyond, "a building in that land, entered from it");
        keep.lastLandKnown = false;
        Require(PlaceBars(keep) == Barred::No, "a game loaded inside: the land is not known, nothing is held against the place");
        Place solstheim;
        solstheim.inAHold = false;    // no hold of the game, but a land ships sail to
        Require(PlaceBars(solstheim) == Barred::No, "Solstheim: people of the island come");
        Place canyon;                 // a valley of its own that lies in a hold of Skyrim
        canyon.landWalkable = false;
        Require(PlaceBars(canyon) == Barred::No, "a place of a hold is within reach, whatever map it is drawn on");
        Place home;                   // a house without a hold above it, entered from Skyrim
        home.outdoors = false;
        home.inAHold = false;
        home.lastLandKnown = true;
        Require(PlaceBars(home) == Barred::No, "a house of a mod with no hold above it: still Skyrim");
        Place crypt;
        crypt.outdoors = false;
        crypt.dungeon = true;
        Require(PlaceBars(crypt) == Barred::Dungeon, "a crypt: whoever has something for the player waits until they are out");
        crypt.classic = true;
        Require(PlaceBars(crypt) == Barred::No, "at Classic the old setting decides who is sent into a dungeon");
        Require(Word(Barred::Beyond)[0] != 0 && Word(Barred::Dungeon)[0] != 0 && Word(Barred::No)[0] == 0, "each has its word");
    } catch (const std::exception& e) {
        std::cerr << "PatrolsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PatrolsCheck passed\n";
    return 0;
}
