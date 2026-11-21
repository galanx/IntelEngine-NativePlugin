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
    } catch (const std::exception& e) {
        std::cerr << "PatrolsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PatrolsCheck passed\n";
    return 0;
}
