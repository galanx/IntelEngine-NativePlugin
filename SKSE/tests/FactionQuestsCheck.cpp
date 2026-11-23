// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/FactionQuestsCheck.cpp
#include "FactionQuests.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace IntelEngine::FactionQuests;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        // Who asks: only a faction the player stands well with (3.8's "friendly": 40 or better).
        Require(!Due(39, true, 10.0f, -1.0f, 0) && !Due(-80, true, 10.0f, -1.0f, 0), "a faction that does not stand well with the player asks nothing");
        Require(Due(40, false, 10.0f, -1.0f, 0), "at 40 it may");
        Require(Chance(39, true) == 0.0f && Chance(40, false) == 0.12f && Chance(55, false) == 0.18f && Chance(75, false) == 0.25f,
                "the share of moments by standing");
        Require(Chance(40, true) > Chance(40, false) && Chance(100, true) < 0.5f, "more while there is a war, never most moments");
        int asked = 0;
        for (std::uint32_t roll = 0; roll < 10000; ++roll) asked += Due(60, false, 10.0f, -1.0f, roll) ? 1 : 0;
        Require(asked == 1800, "18 in a hundred at 60 in peace");
        // Not one on the heels of another.
        Require(!Due(90, true, 10.0f, 9.0f, 0) && !Due(90, true, 10.0f, 7.01f, 0), "not within three days of the last one");
        Require(Due(90, true, 10.0f, 7.0f, 0), "after three days again");
        Require(Due(90, true, 3.0f, 9.0f, 0), "the clock was set back: the last one is not held against them");

        // What: nothing without an enemy; a battle only in a war with no battle about; the rest needs a place.
        Climate c;
        Require(Pick(c, 0) == Kind::None, "no enemy, no quest");
        c.enemy = true;
        Require(Pick(c, 0) == Kind::None, "an enemy but no fort, no captive and no war: nothing to ask");
        c.atWar = true;
        Require(Pick(c, 1) == Kind::None, "a war, but a battle of theirs is about already");
        c.battleFree = true;
        Require(Pick(c, 0) == Kind::Battle && Pick(c, 1) == Kind::Battle, "a war and the field free: a battle, the only kind that fits");
        c.fort = true;
        Require(Pick(c, 0) == Kind::Battle && Pick(c, 1) == Kind::Combat, "a war and a fort: a battle or a clear-out, as the seed falls");
        c.captive = true;
        bool rescue = false, combat = false;
        for (std::uint32_t seed = 1; seed < 40; seed += 2) {
            rescue = rescue || Pick(c, seed) == Kind::Rescue;
            combat = combat || Pick(c, seed) == Kind::Combat;
        }
        Require(rescue && combat, "with somebody who may be held, a rescue now and then beside the clear-outs");
        // The switches of the settings.
        c.battleOn = false;
        Require(Pick(c, 0) != Kind::Battle && Pick(c, 0) != Kind::None, "the battle switch off: never a battle");
        c.combatOn = false;
        Require(Pick(c, 0) == Kind::Rescue && Pick(c, 1) == Kind::Rescue, "only the rescue left");
        c.rescueOn = false;
        Require(Pick(c, 0) == Kind::None, "all three off: nothing");
        c.rescueOn = true;
        c.captive = false;
        Require(Pick(c, 0) == Kind::None, "a rescue needs somebody who may be held");
        Require(SubType(Kind::Combat) == std::string_view("faction_combat") && SubType(Kind::Rescue) == std::string_view("faction_rescue") &&
                    SubType(Kind::Battle) == std::string_view("faction_battle") && SubType(Kind::None) == std::string_view(""),
                "the sub-types the 3.8 handler knows");
    } catch (const std::exception& e) {
        std::cerr << "FactionQuestsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "FactionQuestsCheck passed\n";
    return 0;
}
