// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/StoryCandidateResolutionCheck.cpp
#include "StoryCandidateResolution.h"

#include <iostream>
#include <stdexcept>
#include <vector>

// Frozen pre-#41 selection algorithm: diagnostics must not change selection.
static std::uint32_t OriginalResolve(std::string_view name,
    const IntelEngine::StoryCandidatePool& dm, const IntelEngine::StoryCandidatePool& npc) {
    std::string lower(name);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const bool exact : {true, false}) {
        const auto normalized = exact ? std::string{} : IntelEngine::NormalizeStoryCandidateName(name);
        if (!exact && normalized.empty()) return 0;
        std::uint32_t match = 0;
        bool found = false;
        for (const auto* pool : {&dm, &npc}) {
            for (const auto& [candidate, formId] : *pool) {
                if (exact ? candidate != lower : IntelEngine::NormalizeStoryCandidateName(candidate) != normalized) continue;
                if (formId == 0 || found) return 0;
                match = formId;
                found = true;
            }
        }
        if (found) return match;
    }
    return 0;
}

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
    Require(ResolveStoryCandidateFormID("Maering", {{"maering", 42}}, {{"maering", 42}}) == 0,
            "same-actor duplicate entries must continue to reject");
    const std::vector<StoryCandidatePool> pools{
        {}, {{"maering", 0}}, {{"maering", 42}}, {{"maering", 0xFE000801}},
        {{"maering [soldier]", 42}}, {{"maering [soldier]", 42}, {"maering [guard]", 43}},
        {{"maering", 42}, {"maering", 42}}, {{"maering", 42}, {"maering", 0}},
        {{"unknown", 99}}
    };
    std::size_t comparisons = 0;
    for (const auto& left : pools) for (const auto& right : pools) {
        for (const auto query : {"Maering", "Maering [Soldier]", " \tMAERING  ", "Unknown", "", " \t"}) {
            StoryCandidateRejection diagnostic;
            Require(ResolveStoryCandidateFormID(query, left, right, &diagnostic) ==
                    OriginalResolve(query, left, right), "diagnostics changed candidate selection");
            ++comparisons;
        }
    }
    for (const auto branch : {"empty-pools", "no-match", "ambiguous", "zero-formid"}) {
        StoryCandidateRejection diagnostic;
        const std::string_view expected = branch;
        StoryCandidatePool left, right;
        if (expected == "no-match") left.emplace("unknown", 1);
        if (expected == "zero-formid") left.emplace("maering", 0);
        if (expected == "ambiguous") { left.emplace("maering", 42); right = left; }
        Require(ResolveStoryCandidateFormID("Maering", left, right, &diagnostic) == 0 &&
                diagnostic.branch == expected, "wrong rejection diagnostic");
    }
    std::cout << "PASS: " << comparisons << " pre-change selection comparisons and rejection branches\n";
    std::cout << "PASS: exact, suffix-dropped Avresa/Maering, case/whitespace, ambiguous, duplicate, unknown and out-of-set rejection\n";
}
