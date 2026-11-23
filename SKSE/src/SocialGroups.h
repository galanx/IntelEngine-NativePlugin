#pragma once

// Who the social engine offers the model for a scene between two NPCs (pure: no game types, checked by
// SocialGroupsCheck).
//
// People are offered in groups, one per place, anywhere in the loaded world: a rumor goes round Riften while the
// player is in Riverwood, and that is the point of it. What went wrong was which places were offered. They were
// ranked by headcount, and on a list of some 1,500 plugins the fullest "places" are no places at all: a mod's back
// room of thirty figures all named "Planter", a test cell of identical soldiers, a garrison at the other end of the
// map. They took three of the four groups of every request (2026-11-23), the model was paid to read them, and the
// one real group it then chose from could not be told apart from them (StoryCandidateResolution.h).
//
// So: in a place, people of one name are one entry (three "Imperial Soldier" are nobody in particular, and a name
// offered twice cannot be answered); a place needs two people and at least one who is somebody; and a crowd counts
// no further than four, by who its people are, not by how many stand there.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace IntelEngine::SocialGroups {
    struct Person {
        std::uint32_t id = 0;
        std::string nameLower;
        std::string placeLower;
        bool unique = false;        // the game's own mark of a one-of-a-kind person
        bool withPlayer = false;    // in the cell the player is in
        float social = 0.f;         // how much they have been part of lately (MemoryDB's social score)
    };

    struct Group {
        std::string placeLower;
        std::vector<std::size_t> people;    // into the list given, the most storied first, one of a name
        float score = 0.f;
        bool playerNearby = false;
    };

    inline constexpr std::size_t kCounted = 4;      // a place's crowd counts no further than this
    inline constexpr float kSomebody = 1.0f;
    inline constexpr float kAnybody = 0.35f;        // one of several of a name: a guard, a soldier
    inline constexpr float kSocial = 0.3f;
    inline constexpr float kNearPlayer = 2.0f;      // scenes the player can watch are worth more, never all there is
    inline constexpr float kTurns = 2.0f;           // how far chance may lift a place: a small inn gets its turn too

    // noise: a number from 0 to 1 a call, so that places of like worth take turns (none: no chance in it).
    inline std::vector<Group> Build(const std::vector<Person>& people, std::size_t maxGroups, const std::function<float()>& noise = {}) {
        // Somebody: marked one of a kind by the game, or the only one of their name in the whole loaded world (a mod's
        // named person whose author never set the mark).
        std::unordered_map<std::string, int> carried;
        for (const auto& p : people) ++carried[p.nameLower];
        const auto somebody = [&](std::size_t i) { return people[i].unique || carried[people[i].nameLower] == 1; };
        const auto before = [&](std::size_t a, std::size_t b) {
            const bool sa = somebody(a), sb = somebody(b);
            if (sa != sb) return sa;
            if (people[a].social != people[b].social) return people[a].social > people[b].social;
            return people[a].id < people[b].id;
        };

        std::unordered_map<std::string, std::vector<std::size_t>> places;
        for (std::size_t i = 0; i < people.size(); ++i) {
            if (people[i].id != 0 && !people[i].nameLower.empty() && !people[i].placeLower.empty()) places[people[i].placeLower].push_back(i);
        }

        std::vector<Group> groups;
        for (auto& [place, all] : places) {
            Group g;
            g.placeLower = place;
            std::unordered_map<std::string, std::size_t> byName;      // one of a name: the best of them
            for (const auto i : all) {
                if (people[i].withPlayer) g.playerNearby = true;
                const auto [it, fresh] = byName.emplace(people[i].nameLower, i);
                if (!fresh && before(i, it->second)) it->second = i;
            }
            for (const auto& entry : byName) g.people.push_back(entry.second);
            if (g.people.size() < 2) continue;
            std::sort(g.people.begin(), g.people.end(), before);
            if (!somebody(g.people.front())) continue;                // nobody there is anybody in particular
            for (std::size_t n = 0; n < g.people.size() && n < kCounted; ++n) {
                g.score += (somebody(g.people[n]) ? kSomebody : kAnybody) + kSocial * people[g.people[n]].social;
            }
            if (g.playerNearby) g.score += kNearPlayer;
            groups.push_back(std::move(g));
        }
        // Noise after the places are in a fixed order, so that a given run of noise gives a given answer.
        std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.placeLower < b.placeLower; });
        if (noise) {
            for (auto& g : groups) g.score += kTurns * noise();
        }
        std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.score > b.score; });
        if (groups.size() > maxGroups) groups.resize(maxGroups);
        return groups;
    }
}
