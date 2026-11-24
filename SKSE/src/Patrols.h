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

    // ------------------------------------------------------------------ private moments
    //
    // The user, 2026-11-22: "users were complaining of soldiers spawning during their private moments". Whoever the
    // engine makes that comes at the player in arms (a faction's patrol, a skirmish of two factions near the player,
    // the hired men of a story, a killer after a leader the player stands by) comes upon a traveller: never upon
    // somebody who is with somebody, or has settled somewhere. Decided from what the game itself shows, so it holds
    // with any mod that makes such moments (an intimacy mod's scene, a camp, a bath, a long talk):
    //  - a scene with the player is running (the game's, or an intimacy mod's), or ended within ten minutes;
    //  - somebody spoke to the player, or the player spoke, within the last two minutes (a SkyrimNet conversation
    //    has no menu of the game to tell it by);
    //  - the player sits, lies or sleeps;
    //  - the player wears nothing on the body (bathing, or more private than that);
    //  - the player has stayed where they were for the last minute: whoever has stopped has stopped for something.
    //    Going through a door in that minute is travelling. Right after a load nothing is known yet: nobody comes.
    struct Privacy {
        bool scene = false;          // a scene with the player is running
        float sinceScene = -1.0f;    // real seconds since the last one was seen or ended; below 0: none this session
        float sinceTalk = -1.0f;     // real seconds since somebody spoke to the player or the player spoke; below 0: never
        bool resting = false;        // sitting, lying, asleep
        bool undressed = false;      // nothing worn on the body
        bool tracked = false;        // where the player goes is being followed (false: nothing is held against them for it)
        float moved = -1.0f;         // how far they are from where they were about a minute ago; below 0: not known yet
        bool crossed = false;        // they went through a door, or to another land, in that minute
    };

    enum class Private : std::uint8_t { No = 0, Scene, AfterScene, Talk, Resting, Undressed, Settled, JustArrived };

    constexpr float kAfterSceneSeconds = 600.0f;
    constexpr float kAfterTalkSeconds = 120.0f;
    constexpr float kOnTheMove = 1000.0f;   // units in a minute: a slow walk covers four times that

    inline constexpr Private PrivateMoment(const Privacy& p) {
        if (p.scene) return Private::Scene;
        if (p.sinceScene >= 0.0f && p.sinceScene < kAfterSceneSeconds) return Private::AfterScene;
        if (p.sinceTalk >= 0.0f && p.sinceTalk < kAfterTalkSeconds) return Private::Talk;
        if (p.resting) return Private::Resting;
        if (p.undressed) return Private::Undressed;
        if (p.tracked && !p.crossed) {
            if (p.moved < 0.0f) return Private::JustArrived;
            if (p.moved < kOnTheMove) return Private::Settled;
        }
        return Private::No;
    }

    inline constexpr const char* Word(Private why) {
        switch (why) {
            case Private::Scene: return "a scene with the player is running";
            case Private::AfterScene: return "a scene with the player ended a short while ago";
            case Private::Talk: return "the player is in talk with somebody";
            case Private::Resting: return "the player sits, lies or sleeps";
            case Private::Undressed: return "the player wears nothing";
            case Private::Settled: return "the player has stayed where they are for a while";
            case Private::JustArrived: return "nothing is known yet of where the player has been this last minute";
            default: return "";
        }
    }

    // ------------------------------------------------------------------ places nobody is sent to
    //
    // A user, 2026-11-20: "npcs spawn in dangerous places to talk to me and they die / im in the soul carion / how
    // did they even manage to get here". Nobody is sent to the player, and nobody on the way is set down beside them:
    //  - beyond reach: a place that lies in no hold and in no land one can walk or sail to from the towns (the Soul
    //    Cairn, Apocrypha, Sovngarde, the vale behind a mountain, a mod's own island). At every level of the living
    //    world, whatever the settings say: there is no road there. Indoors the land is the last one seen out of
    //    doors; where that is not known (a game loaded inside), nothing is held against the place;
    //  - a dungeon (the game's own kinds: a cave, a crypt, a ruin, a camp of bandits, a lair): above "Classic" whoever
    //    has something for the player waits until they are out again (leaving a dungeon brings a look of its own).
    //    At "Classic" the setting "NPCs sent into dangerous places" decides, as before 4.0.
    // Whoever is already there with the player is not sent anywhere and is free to speak.
    struct Place {
        bool outdoors = true;
        bool inAHold = true;          // the place has a hold of the game above it
        bool landWalkable = true;     // out of doors: the land is Skyrim's or Solstheim's
        bool lastLandKnown = false;   // indoors: a land was seen out of doors this session
        bool lastLandWalkable = true;
        bool dungeon = false;
        bool classic = false;
    };
    enum class Barred : std::uint8_t { No = 0, Dungeon = 1, Beyond = 2 };

    inline constexpr Barred PlaceBars(const Place& p) {
        if (!p.inAHold) {
            const bool beyond = p.outdoors ? !p.landWalkable : (p.lastLandKnown && !p.lastLandWalkable);
            if (beyond) return Barred::Beyond;
        }
        if (p.dungeon && !p.classic) return Barred::Dungeon;
        return Barred::No;
    }

    inline constexpr const char* Word(Barred why) {
        return why == Barred::Beyond ? "the player is where nobody could have followed" : why == Barred::Dungeon ? "the player is in a dungeon" : "";
    }

    // roll: 0 to 9999, drawn afresh each time. lastDays below 0: no patrol yet (or not known after a load).
    inline constexpr bool Due(int standing, float nowDays, float lastDays, std::uint32_t roll) {
        if (standing > kHostile) return false;
        if (lastDays >= 0.0f && nowDays >= lastDays && nowDays - lastDays < kRestDays) return false;
        return static_cast<float>(roll % 10000u) < Chance(standing) * 10000.0f;
    }

}
