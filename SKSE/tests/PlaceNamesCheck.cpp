// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/PlaceNamesCheck.cpp
#include "PlaceNames.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::PlaceTargets;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        Require(Fold("  The   Kitchen. ") == "kitchen", "case, spaces, article and full stop go");
        Require(Fold("Theodor's Rest") == "theodor's rest", "only a separate leading 'the' goes");
        Require(Fold("") == "" && Fold("   ") == "", "nothing stays nothing");

        Require(Names(Fold("the kitchen"), "Kitchen", "Breezehome"), "the place's own name");
        Require(Names(Fold("Kitchen in Breezehome"), "Kitchen", "Breezehome"), "name in location");
        Require(Names(Fold("Breezehome kitchen"), "Kitchen", "Breezehome"), "location then name");
        Require(Names(Fold("Breezehome's kitchen"), "Kitchen", "Breezehome"), "location's name");
        Require(Names(Fold("the camp at Riverwood"), "Camp", "Riverwood"), "name at location");
        Require(!Names(Fold("kitchen in Dragonsreach"), "Kitchen", "Breezehome"), "another location's kitchen is not it");
        Require(!Names(Fold("kit"), "Kitchen", "Breezehome"), "no partial names");
        Require(!Names(Fold("kitchen"), "", "Breezehome"), "a place without a name matches nothing");
        Require(!Names(Fold("Breezehome kitchen"), "Kitchen", ""), "without a location only the name itself");
    } catch (const std::exception& e) {
        std::cerr << "PlaceNamesCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PlaceNamesCheck passed\n";
    return 0;
}
