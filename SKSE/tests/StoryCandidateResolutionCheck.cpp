// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/StoryCandidateResolutionCheck.cpp
#include "StoryCandidateResolution.h"

#include <iostream>
#include <stdexcept>
#include <vector>


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
    for (const auto id : {42u, 0xFE000801u, 0xFF000801u}) {
        Require(ResolveStoryCandidateFormID("Maering", {{"maering", id}, {"maering", id}}, {{"maering", id}}) == id,
                "exact repeated reference identity must resolve within and across pools");
        Require(ResolveStoryCandidateFormID(" MAERING ", {{"maering [soldier]", id}, {"maering [guard]", id}}, {{"maering [mage]", id}}) == id,
                "normalized repeated reference identity must resolve within and across pools");
    }
    Require(ResolveStoryCandidateFormID("Maering", {{"maering", 42}}, {{"maering", 0}}) == 0,
            "zero ID mixed with a valid reference must reject");
    StoryCandidatePool twins{{"guard", 1}, {"guard", 2}, {"guard", 2}, {"guard", 3}};
    const auto nearest = [&](const StoryCandidatePool& pool, double anchor, std::uint32_t self = 0) {
        return ResolveStoryCandidateFormID("Guard", pool, {}, nullptr, [=](std::uint32_t id) {
            const double x = id == 1 ? 0 : id == 2 ? 10 : 20;
            return id == self || id > 3 ? -1.0 : (x - anchor) * (x - anchor);
        });
    };
    Require(nearest(twins, 9) == 2, "NPC 1 nearest player despite repeated ID");
    Require(nearest(twins, 10, 2) == 1, "NPC 2 nearest partner excludes self and ties by lowest ID");
    Require(nearest(twins, 20, 3) == 2, "partner, not player, determines NPC 2");
    StoryCandidatePool reversed;
    for (const auto id : {3u, 2u, 1u}) reversed.emplace("guard", id);
    Require(nearest(reversed, 10, 2) == nearest(twins, 10, 2), "candidate order must not affect ranking");
    Require(nearest({{"guard", 2}}, 10, 2) == 0, "self-only pair rejects");
    Require(nearest({{"guard", 4}, {"guard", 5}}, 10) == 0, "incomparable or invalid candidates reject");
    Require(nearest({{"guard", 0}, {"guard", 2}}, 10) == 0, "proximity must preserve zero-ID rejection");
    // fix63: bare name with a DM-shown actor must not pick a nearer same-name actor the DM never saw.
    const auto gamlunda = [](const StoryCandidatePool& shown) {
        return ResolveStoryCandidateFormID("Gamlunda", shown, {{"gamlunda [servant]", 1}}, nullptr,
                                           [](std::uint32_t id) { return id == 1 ? 1.0 : 100.0; });
    };
    Require(gamlunda({{"gamlunda [priest]", 2}}) == 2, "bare name must resolve to the DM-shown actor, not the nearer unseen one");
    Require(gamlunda({}) == 1, "no DM-shown match keeps the social-pool fallback");
    std::cout << "PASS: production proximity ranking, ambiguous names, partner anchor, ties, duplicates and exclusions\n";
    for (const auto branch : {"empty-pools", "no-match", "ambiguous", "zero-formid"}) {
        StoryCandidateRejection diagnostic;
        const std::string_view expected = branch;
        StoryCandidatePool left, right;
        if (expected == "no-match") left.emplace("unknown", 1);
        if (expected == "zero-formid") left.emplace("maering", 0);
        if (expected == "ambiguous") { left.emplace("maering", 42); right.emplace("maering", 43); }
        Require(ResolveStoryCandidateFormID("Maering", left, right, &diagnostic) == 0 &&
                diagnostic.branch == expected, "wrong rejection diagnostic");
    }
    std::cout << "PASS: repeated reference identities, zero-ID rejection and rejection branches\n";

    // ---- The social engine's answer: two of one group (2026-11-23, a list of some 1,500 plugins).
    // Three people named Sven and two named Delphine are in the game: the real ones in Riverwood's inn (group 0), a
    // mod's copies in a back room (group 1), another mod's Sven in its own town (group 3). The model chose
    // "Delphine" and "Sven" of the inn, fourteen times, and the old rule answered "nobody" each time.
    {
        const StoryCandidatePool offered{{"sven", 0x00013491},     {"delphine", 0x00013485}, {"feier", 0xFE0AA809},
                                         {"jinx", 0xFEDA9803},     {"sven", 0x241A1166},     {"delphine", 0x2419A8F4},
                                         {"planter", 0x24000001},  {"marise aravel", 0x000136C0},
                                         {"nivenor", 0x000136C1},  {"sven", 0x6C122399},     {"reich guard", 0x6C000010}};
        const StoryCandidateGroups groupOf{{0x00013491, 0}, {0x00013485, 0}, {0xFE0AA809, 0}, {0xFEDA9803, 0},
                                           {0x241A1166, 1}, {0x2419A8F4, 1}, {0x24000001, 1},
                                           {0x000136C0, 2}, {0x000136C1, 2}, {0x6C122399, 3}, {0x6C000010, 3}};
        Require(ResolveStoryCandidateFormID("delphine", {}, offered) == 0, "the old rule: a name two carry is nobody (what was seen)");
        auto pair = ResolveStoryPair("delphine", "Sven", offered, groupOf);
        Require(pair && pair.first == 0x00013485 && pair.second == 0x00013491 && pair.told && std::string_view(pair.how) == "same-group",
                "Delphine and Sven of the inn, the first group that holds both");
        pair = ResolveStoryPair("Sven", "FeiEr", offered, groupOf);
        Require(pair && pair.first == 0x00013491 && pair.second == 0xFE0AA809, "the Sven who stands with FeiEr");
        pair = ResolveStoryPair("Sven", "Reich Guard", offered, groupOf);
        Require(pair && pair.first == 0x6C122399 && pair.second == 0x6C000010, "the other town's Sven with his own guard");
        pair = ResolveStoryPair("Sven", "Planter", offered, groupOf);
        Require(pair && pair.first == 0x241A1166, "the group decides, whichever it is");
        pair = ResolveStoryPair("Marise Aravel", "Nivenor", offered, groupOf);
        Require(pair && pair.first == 0x000136C0 && pair.second == 0x000136C1 && !pair.told, "one of a name each: as before");
        pair = ResolveStoryPair("Marise Aravel", "Jinx", offered, groupOf);
        Require(pair && std::string_view(pair.how) == "by-name", "two of different groups, each the only one of their name: taken, as before");
        pair = ResolveStoryPair("Sven", "Nivenor", offered, groupOf);
        Require(!pair && std::string_view(pair.how) == "ambiguous", "no group holds both and three are called Sven: nobody");
        pair = ResolveStoryPair("Sven", "Sven", offered, groupOf);
        Require(!pair, "nobody meets themselves, and no group has two of the name");
        pair = ResolveStoryPair("Hulda", "Sven", offered, groupOf);
        Require(!pair && std::string_view(pair.how) == "no-match", "a name nobody offered carries");
        pair = ResolveStoryPair("Jinx", "Jinx", offered, groupOf);
        Require(!pair && std::string_view(pair.how) == "same-person", "one person named twice");
        Require(ResolveStoryPartner("Sven", 0x00013485, offered, groupOf) == 0x00013491, "the second one: of the first one's group");
        Require(ResolveStoryPartner("delphine", 0x241A1166, offered, groupOf) == 0x2419A8F4, "... whichever group that is");
        Require(ResolveStoryPartner("Nivenor", 0x00013485, offered, groupOf) == 0x000136C1, "... else the only one of the name");
        Require(ResolveStoryPartner("Sven", 0x000136C0, offered, groupOf) == 0, "... and nobody when several carry it elsewhere");
        Require(ResolveStoryPartner("Jinx", 0xFEDA9803, offered, groupOf) == 0, "never the first one again");
        // A name with its role in brackets, and a name with an ending (Albanian speech in an English game).
        const StoryCandidatePool roles{{"brend [imperial soldier]", 1}, {"maering [imperial soldier]", 2}, {"brend [guard]", 3}};
        pair = ResolveStoryPair("Brend", "Maering", roles, {{1, 0}, {2, 0}, {3, 1}});
        Require(pair && pair.first == 1 && pair.second == 2, "the role left out: still the one of the group");
        pair = ResolveStoryPair("Svenit", "Delphin\xC3\xABn", offered, groupOf);     // Delphinen, with a diaeresis on the e
        Require(pair && pair.first == 0x00013491 && pair.second == 0x00013485, "a name in another form finds the same people");
        std::cout << "PASS: the social engine's pair is found in the group it was offered in\n";
    }
    std::cout << "PASS: exact, suffix-dropped Avresa/Maering, case/whitespace, ambiguous, duplicate, unknown and out-of-set rejection\n";
}
