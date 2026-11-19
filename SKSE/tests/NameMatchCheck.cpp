// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/NameMatchCheck.cpp
#include "NameMatch.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::NameMatch;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        // ---- a name inside another, as whole words
        Require(WholeWords("jon battle-born", "jon") && WholeWords("jon battle-born", "battle-born") && WholeWords("jon battle-born", "born"), "a first name, a clan name");
        Require(WholeWords("lydia the housecarl", "lydia") && WholeWords("jarl balgruuf the greater", "balgruuf"), "a name with a title");
        Require(WholeWords("lydia", "lydia"), "itself");
        // The reports: "Garth" came for "Gollgarth"; a modded name with "tsun" inside it answered for Tsun.
        Require(!WholeWords("gollgarth", "garth") && !WholeWords("kaitsun the bold", "tsun") && !WholeWords("brotsun", "tsun"), "never part of a word");
        Require(!WholeWords("edan's shadow", "edan") && WholeWords("edan's shadow", "shadow") && WholeWords("edan's shadow", "edan's"), "an apostrophe belongs to its word");
        Require(!WholeWords("jon", "jon battle-born") && !WholeWords("jon", ""), "the longer is never inside the shorter; nothing is no name");
        Require(PartOf("jon", "jon battle-born") && PartOf("jon battle-born", "jon") && !PartOf("garth", "gollgarth"), "either way round");
        // Letters outside ASCII are letters: a Cyrillic name is not cut in the middle of a word.
        Require(WholeWords("\xD0\x9B\xD0\xB8\xD0\xB4\xD0\xB8\xD1\x8F \xD1\x85\xD1\x83\xD1\x81\xD0\xBA\xD0\xB0\xD1\x80\xD0\xBB", "\xD0\x9B\xD0\xB8\xD0\xB4\xD0\xB8\xD1\x8F"), "a whole word in Cyrillic");
        Require(!WholeWords("\xD0\x9B\xD0\xB8\xD0\xB4\xD0\xB8\xD1\x8F", "\xD0\xB8\xD0\xB4\xD0\xB8"), "not the middle of one");

        // ---- a slip of the pen, by the length of the name
        Require(Slips(3, 3) == 0 && Slips(4, 3) == 0, "a short name is itself or nothing (Tsun, Edda, Jon)");
        Require(Slips(5, 3) == 1 && Slips(7, 3) == 1 && Slips(8, 3) == 2 && Slips(11, 3) == 2 && Slips(12, 3) == 3, "more room in a longer name");
        Require(Slips(20, 1) == 1 && Slips(20, 0) == 0, "never more than the setting allows");
        Require(Slips("tsun", "tsundar the old", 3) == 0 && Slips("balgruff", "balgruuf", 3) == 2, "measured on the shorter of the two");
    } catch (const std::exception& e) {
        std::cerr << "NameMatchCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "NameMatchCheck passed\n";
    return 0;
}
