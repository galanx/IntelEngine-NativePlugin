// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/StoryCandidateResolutionCheck.cpp
#include "StoryCandidateResolution.h"

#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace IntelEngine;
    const StoryCandidatePool dm{{"avresa caeparia [priest]", 0x1200C028}};
    const StoryCandidatePool social{{"brend [imperial soldier]", 0x0010C06C},
                                    {"maering [imperial soldier]", 0x0010C06D}};
    const auto resolve = [&](std::string_view name) { return ResolveStoryCandidateFormID(name, dm, social); };
    Require(resolve("Avresa Caeparia [Priest]") == 0x1200C028, "exact display match");
    Require(resolve("Avresa Caeparia") == 0x1200C028, "Avresa must not become Ria 0001A6D8");
    Require(resolve("Maering") == 0x0010C06D, "Maering must not become Hadring 0002005A");
    Require(resolve(" \tAVRESA   Caeparia\n") == 0x1200C028, "case and whitespace normalization");
    Require(resolve("Ria") == 0 && resolve("Hadring") == 0 && resolve("Unknown") == 0,
            "out-of-set actors must reject");
    Require(resolve("") == 0 && resolve(" \t") == 0, "empty names must reject");
    StoryCandidatePool ambiguous{{"maering [soldier]", 1}, {"maering [guard]", 2}};
    Require(ResolveStoryCandidateFormID("Maering", ambiguous, {}) == 0, "normalized ambiguity must reject");
    Require(ResolveStoryCandidateFormID("Maering [Soldier]", ambiguous, {}) == 1,
            "exact match must precede normalized ambiguity");
    ambiguous.emplace("maering [soldier]", 3);
    Require(ResolveStoryCandidateFormID("Maering [Soldier]", ambiguous, {}) == 0,
            "duplicate display names must not overwrite and select one actor");
    Require(ResolveStoryCandidateFormID("Maering", social, {{"maering [guard]", 4}}) == 0,
            "cross-pool ambiguity must reject");
    Require(ResolveStoryCandidateFormID("Maering", {}, {}) == 0, "empty pools must reject");
    std::cout << "PASS: exact, suffix-dropped Avresa/Maering, case/whitespace, ambiguous, duplicate, unknown and out-of-set rejection\n";
}
