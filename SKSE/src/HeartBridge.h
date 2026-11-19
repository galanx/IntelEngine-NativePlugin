#pragma once

// IntelEngine: reads what stands between the player and someone from the game and from whichever mods keep it
// (Hearts.h has the rules). Everything is read softly: a mod that is not installed reads as "nothing there".
// Nothing of anybody's state is written.

#include "Hearts.h"

namespace RE {
    class Actor;
}

namespace IntelEngine::HeartBridge {

    // Game thread.
    Hearts::Heart HeartOf(RE::Actor* actor);
    Hearts::Who WhoOf(RE::Actor* actor);
    Hearts::Rules RulesNow();
    Hearts::Lean LeanFor(RE::Actor* actor);
    // The line the author is told about them and the player; empty when there is nothing to tell or they must not be
    // part of any story of the heart (then the author is told nothing that could start one).
    std::string WordsFor(RE::Actor* actor);
    // A child by any sign: the game's flag, the race, the voice, the size.
    bool Childlike(RE::Actor* actor);

    // What only scripts can read (other mods keep it in script storage): told by IntelEngine_Arcs about once a
    // minute for everyone a relationship mod tracks. Any thread.
    void Told(std::uint32_t actor, int sparked, int stance, int withChild, bool childIsPlayers, int children, bool playersChild);
    // A save was loaded or the game went back to the menu: what was told belongs to another timeline.
    void Forget();

}
