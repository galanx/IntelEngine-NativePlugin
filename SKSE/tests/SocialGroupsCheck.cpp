// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I SKSE/src SKSE/tests/SocialGroupsCheck.cpp
//
// Which places the social engine offers the model for a scene between two NPCs (SocialGroups.h). The first case is
// a play session on a list of some 1,500 plugins (2026-11-23): three of the four groups of every request were a
// museum mod's back room of figures named "Planter", a test cell of soldiers and a far garrison, chosen by headcount.
#include "SocialGroups.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine::SocialGroups;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Person P(std::uint32_t id, const char* name, const char* place, bool unique, float social = 0.f, bool withPlayer = false) {
    Person p;
    p.id = id;
    p.nameLower = name;
    p.placeLower = place;
    p.unique = unique;
    p.social = social;
    p.withPlayer = withPlayer;
    return p;
}

static const Group* Of(const std::vector<Group>& groups, const char* place) {
    for (const auto& g : groups) {
        if (g.placeLower == place) return &g;
    }
    return nullptr;
}

int main() {
    try {
        // ---- the session: the player is up at Bleak Falls Barrow, nobody of these is with them
        std::vector<Person> world;
        std::uint32_t id = 1;
        for (int i = 0; i < 30; ++i) world.push_back(P(id++, "planter", "utility cell - lotd", false));
        for (int i = 0; i < 12; ++i) world.push_back(P(id++, "imperial soldier", "testing bandolier stuff", false));
        for (int i = 0; i < 9; ++i) world.push_back(P(id++, "imperial soldier", "cyrodiil north border garrison", false));
        for (int i = 0; i < 6; ++i) world.push_back(P(id++, "stormcloak soldier", "sovngarde", false));
        for (int i = 0; i < 6; ++i) world.push_back(P(id++, "imperial soldier", "sovngarde", false));
        world.push_back(P(id++, "sven", "sleeping giant inn", true, 2.0f));
        world.push_back(P(id++, "delphine", "sleeping giant inn", true, 1.5f));
        world.push_back(P(id++, "feier", "sleeping giant inn", true, 3.0f));
        world.push_back(P(id++, "jinx", "sleeping giant inn", false, 1.0f));       // a mod's follower, no mark of her own
        world.push_back(P(id++, "marise aravel", "riften", true));
        world.push_back(P(id++, "nivenor", "riften", true));
        world.push_back(P(id++, "reekeepa", "riften", false));                     // one of a kind by name alone
        for (int i = 0; i < 8; ++i) world.push_back(P(id++, "riften guard", "riften", false));
        world.push_back(P(id++, "hod", "riverwood", true));                        // alone in his place

        auto groups = Build(world, 4);
        Require(groups.size() == 2, "the inn and Riften: the back room, the test cell, the garrison and Sovngarde are no company");
        Require(!Of(groups, "utility cell - lotd") && !Of(groups, "testing bandolier stuff") &&
                    !Of(groups, "cyrodiil north border garrison") && !Of(groups, "sovngarde") && !Of(groups, "riverwood"),
                "thirty of one name are one, and one is no scene; soldiers of two names are nobody in particular");
        const auto* inn = Of(groups, "sleeping giant inn");
        const auto* riften = Of(groups, "riften");
        Require(inn && riften, "both offered, though the player is in neither: a scene needs no player");
        Require(!inn->playerNearby && !riften->playerNearby, "the player is elsewhere");
        Require(inn->people.size() == 4 && world[inn->people.front()].nameLower == "feier", "the most storied first");
        Require(riften->people.size() == 4, "Marise, Nivenor, Reekeepa, and one guard for the eight");
        int guards = 0;
        for (const auto i : riften->people) guards += world[i].nameLower == "riften guard";
        Require(guards == 1 && world[riften->people.back()].nameLower == "riften guard", "one guard, after the people who are somebody");
        Require(groups.front().placeLower == "sleeping giant inn", "the inn's people have more behind them");

        // ---- headcount decides nothing
        {
            std::vector<Person> crowd;
            std::uint32_t n = 1;
            for (int i = 0; i < 40; ++i) crowd.push_back(P(n++, ("citizen " + std::to_string(i)).c_str(), "solitude", true));
            crowd.push_back(P(n++, "hulda", "the bannered mare", true, 4.0f));
            crowd.push_back(P(n++, "mikael", "the bannered mare", true, 4.0f));
            crowd.push_back(P(n++, "saadia", "the bannered mare", true, 4.0f));
            crowd.push_back(P(n++, "uthgerd", "the bannered mare", true, 4.0f));
            const auto ranked = Build(crowd, 2);
            Require(ranked.size() == 2 && ranked.front().placeLower == "the bannered mare",
                    "four with something behind them outrank forty strangers: a crowd counts to four");
            Require(ranked.back().score == static_cast<float>(kCounted) * kSomebody, "forty count as four");
        }

        // ---- the player's own place is worth more, and never all there is
        {
            std::vector<Person> two;
            two.push_back(P(1, "alvor", "riverwood", true, 0.f, true));
            two.push_back(P(2, "sigrid", "riverwood", true, 0.f, true));
            two.push_back(P(3, "aerin", "riften", true));
            two.push_back(P(4, "mjoll the lioness", "riften", true));
            two.push_back(P(5, "bolli", "riften", true));
            const auto ranked = Build(two, 4);
            Require(ranked.size() == 2 && ranked.front().placeLower == "riverwood" && ranked.front().playerNearby,
                    "two beside the player before three far off");
            Require(ranked.back().placeLower == "riften", "... and the far place is still offered");
            Require(Build(two, 1).front().placeLower == "riverwood", "one group only: the player's");
        }

        // ---- chance gives places of like worth their turn, and no more than that
        {
            std::vector<Person> towns;
            std::uint32_t n = 1;
            for (const char* place : {"dawnstar", "falkreath", "morthal"}) {
                for (int i = 0; i < 3; ++i) towns.push_back(P(n++, (std::string(place) + " person " + std::to_string(i)).c_str(), place, true));
            }
            towns.push_back(P(n++, "soldier a", "a camp", false));
            towns.push_back(P(n++, "soldier b", "a camp", false));     // two of a kind by name alone: somebody each
            int turn = 0;
            const auto first = Build(towns, 1, [&]() { return turn++ == 0 ? 1.0f : 0.0f; });
            turn = 0;
            const auto third = Build(towns, 1, [&]() { return turn++ == 3 ? 1.0f : 0.0f; });
            Require(first.front().placeLower == "a camp" && third.front().placeLower == "morthal",
                    "the place chance favours comes first (places are taken in the order of their names)");
            Require(kTurns < kNearPlayer + kSomebody, "chance alone never lifts an empty place over the player's own");
        }

        // ---- one of a name, wherever the mark is missing
        {
            std::vector<Person> twins;
            twins.push_back(P(1, "sven", "the inn", true, 1.0f));
            twins.push_back(P(2, "sven", "the inn", false));            // a copy without the mark, in the same room
            twins.push_back(P(3, "orgnar", "the inn", true));
            const auto g = Build(twins, 4);
            Require(g.size() == 1 && g.front().people.size() == 2 && g.front().people.front() == 0, "one Sven, the real one");
            std::vector<Person> empty;
            Require(Build(empty, 4).empty() && Build(twins, 0).empty(), "nobody, or no room in the request: nothing");
            twins.push_back(P(0, "ghost", "the inn", true));
            twins.push_back(P(9, "", "the inn", true));
            twins.push_back(P(10, "nowhere man", "", true));
            Require(Build(twins, 4).front().people.size() == 2, "no id, no name or no place: not offered");
        }
        std::cout << "SocialGroupsCheck passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "SocialGroupsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
}
