#pragma once

// IntelEngine: a faction the player stands well with asks them for help now and then (a fort of its enemy to clear, one
// of its people held by that enemy, a battle of its war).
// Pure logic: no game types, no engine calls, so SKSE/tests/FactionQuestsCheck.cpp can run it.
//
// Before 4.0 these were the dungeon master's choices, made from the political climate ("Judge these purely on the
// Political Climate"). The director of 4.0 judges people one by one, each on their own reason, and a faction is
// nobody: above the Classic level a faction's quest came only as the leftover of somebody chosen for a quest of their
// own for whom no story and no quest of the engine fitted, which next to never happened (found 2026-11-23 when 3.8.3
// was held against 4.0). It is decided here, in code, as the faction's patrol is (Patrols.h):
//  - who asks: the faction that stands best with the player, at kFriendly or better (3.8's "friendly factions");
//  - against whom: its enemy at war with it first, else the faction it stands worst with (below zero: 3.8's "hostile
//    factions"), never one the game's own story still binds, never one that is no more;
//  - when: only at a moment the director lets land anyway (its budget, its rest after a big moment, no bad moment),
//    never in a private moment and never where nobody could have come (the story engine asks both first), and of
//    those moments only some: more of them the better the player stands and while there is a war. Not within
//    kRestDays of the last faction quest;
//  - what: a battle while the two are at war and no battle of theirs is gathering already, else a clear-out or a
//    rescue, as the settings allow.
// The quest itself is the 3.8 dungeon master's own (StoryEngine.HandleQuestDispatch): a soldier of the faction who is
// near the player brings it, the place and the people are checked there as before.

#include <cstdint>

namespace IntelEngine::FactionQuests {

    constexpr int kFriendly = 40;        // the standing from which a faction asks the player (3.8's "friendly")
    constexpr float kRestDays = 3.0f;    // game days between two faction quests, of any faction

    enum class Kind : std::uint8_t { None = 0, Combat, Rescue, Battle };

    // Of the moments that may land, the share a faction at this standing takes for a quest (0 to 1).
    inline constexpr float Chance(int standing, bool atWar) {
        if (standing < kFriendly) return 0.0f;
        const float base = standing >= 75 ? 0.25f : standing >= 55 ? 0.18f : 0.12f;
        return atWar ? base + 0.1f : base;
    }

    // roll: 0 to 9999, drawn afresh each time. lastDays below 0: none yet (or not known after a load).
    inline constexpr bool Due(int standing, bool atWar, float nowDays, float lastDays, std::uint32_t roll) {
        if (standing < kFriendly) return false;
        if (lastDays >= 0.0f && nowDays >= lastDays && nowDays - lastDays < kRestDays) return false;
        // In ten-thousandths, rounded: 0.18 is 1800 of them (as a float it is a hair above or below).
        return roll % 10000u < static_cast<std::uint32_t>(Chance(standing, atWar) * 10000.0f + 0.5f);
    }

    struct Climate {
        bool combatOn = true;      // the settings' "Quest kind: fight a faction's enemies"
        bool rescueOn = true;      // "Quest kind: rescue for a faction"
        bool battleOn = true;      // "Quest kind: join a faction's battle"
        bool enemy = false;        // an enemy to send the player against (at war with them, or below zero)
        bool atWar = false;        // that enemy and the faction are at war
        bool battleFree = false;   // no battle is being fought and none of theirs is gathering
        bool fort = false;         // a fort or a camp of the enemy's land (or near it) to send the player to
        bool captive = false;      // somebody of the faction's town who may be held there
    };

    // What the faction asks for. seed: any number (which of the two smaller kinds, when both fit).
    inline constexpr Kind Pick(const Climate& c, std::uint32_t seed) {
        if (!c.enemy) return Kind::None;
        if (c.battleOn && c.atWar && c.battleFree && seed % 2u == 0u) return Kind::Battle;
        const bool combat = c.combatOn && c.fort;
        const bool rescue = c.rescueOn && c.fort && c.captive;
        if (combat && rescue) return (seed / 2u) % 3u == 0u ? Kind::Rescue : Kind::Combat;
        if (combat) return Kind::Combat;
        if (rescue) return Kind::Rescue;
        if (c.battleOn && c.atWar && c.battleFree) return Kind::Battle;
        return Kind::None;
    }

    inline constexpr const char* SubType(Kind k) {
        switch (k) {
            case Kind::Combat: return "faction_combat";
            case Kind::Rescue: return "faction_rescue";
            case Kind::Battle: return "faction_battle";
            default: return "";
        }
    }
}
