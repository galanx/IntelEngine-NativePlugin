#pragma once

// IntelEngine: a faction that holds the player for an enemy sends men after them now and then.
// Pure logic: no game types, no engine calls, so SKSE/tests/PatrolsCheck.cpp can run it.
//
// Before 4.0 this was one of the dungeon master's choices ("faction_ambush": standing with the faction at -40 or
// worse, three to seven of its soldiers). The director of 4.0 judges people one by one, each on their own reason,
// and a faction is nobody: above the Classic level the patrol had no way to happen any more, though nothing had been
// meant to take it away (found 2026-11-21 when 3.8 was held against 4.0). It is decided here, in code:
//  - who: the faction that stands worst with the player, at kHostile or below;
//  - when: only at a moment the director lets land anyway (its budget, its rest after a big moment, no bad moment),
//    and of those only some: the deeper the feud, the more of them. Not within kRestDays of the last patrol;
//  - how many: three or four while the feud is young, up to seven when it is deep (as the 3.8 handler held it).
// The story engine's own checks stay as they were: out of doors, not in or by a town, from a spot the player does
// not see, no battle or other ambush running.

#include <cstdint>

namespace IntelEngine::Patrols {

    constexpr int kHostile = -40;        // the standing from which a faction sends men (3.8's rule)
    constexpr float kRestDays = 2.0f;    // game days between two patrols, of any faction

    // Of the moments that may land, the share a faction at this standing takes for a patrol (0 to 1).
    inline constexpr float Chance(int standing) {
        if (standing > kHostile) return 0.0f;
        if (standing > -60) return 0.2f;
        if (standing > -80) return 0.35f;
        return 0.5f;
    }

    // How many come.
    inline constexpr int Size(int standing, std::uint32_t seed) {
        if (standing > -60) return 3 + static_cast<int>(seed % 2u);     // 3 or 4
        if (standing > -80) return 4 + static_cast<int>(seed % 2u);     // 4 or 5
        return 5 + static_cast<int>(seed % 3u);                         // 5 to 7
    }

    // roll: 0 to 9999, drawn afresh each time. lastDays below 0: no patrol yet (or not known after a load).
    inline constexpr bool Due(int standing, float nowDays, float lastDays, std::uint32_t roll) {
        if (standing > kHostile) return false;
        if (lastDays >= 0.0f && nowDays >= lastDays && nowDays - lastDays < kRestDays) return false;
        return static_cast<float>(roll % 10000u) < Chance(standing) * 10000.0f;
    }

}
