#pragma once

// The game's own civil war, and what the politics simulation may do beside it. The simulation knew nothing of it:
// the Empire and the Thalmor went to war "on the first day", the Stormcloaks "sued for peace" in a war the player
// had not yet fought, and after the player had won it people still spoke as if it were undecided ("the npcs are
// like, what does it matter anyway the imperials have already won"). The user's rule: nothing ever contradicts the
// game's main quests. Pure logic; FactionPolitics reads the facts from the game (the CWObj quest's last stage, the
// CWPlayerAllegiance global, Season Unending and the main quest's end) and holds the simulation to this.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

namespace IntelEngine::VanillaWar {

    enum class State : std::uint8_t { Undecided = 0, Truce = 1, ImperialsWon = 2, StormcloaksWon = 3 };
    enum class Side : std::uint8_t { None = 0, Imperial = 1, Stormcloak = 2, Thalmor = 3 };

    // What the game says. allegiance: the player's side as the game keeps it (0 none, 1 the Empire, 2 the Stormcloaks).
    struct Facts {
        bool over = false;           // the civil war's questline is finished (its winner is the player's side)
        int allegiance = 0;
        bool truce = false;          // the peace council has been held (Season Unending done)
        bool dragonsDone = false;    // the main quest is over: a truce made for its sake no longer holds
    };

    inline State Of(const Facts& f) {
        if (f.over) return f.allegiance == 2 ? State::StormcloaksWon : State::ImperialsWon;
        if (f.truce && !f.dragonsDone) return State::Truce;
        return State::Undecided;
    }

    // Which side of it a faction of the simulation is, by the game faction it stands for (factions.yaml's
    // skyrim_faction_id), whatever the faction is called there.
    inline Side SideOf(std::string engineFaction) {
        for (auto& c : engineFaction) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (engineFaction == "cwimperialfaction") return Side::Imperial;
        if (engineFaction == "cwsonsfaction") return Side::Stormcloak;
        if (engineFaction == "thalmorfaction") return Side::Thalmor;
        return Side::None;
    }

    inline bool Pair(Side a, Side b, Side x, Side y) { return (a == x && b == y) || (a == y && b == x); }
    inline bool CivilWarPair(Side a, Side b) { return Pair(a, b, Side::Imperial, Side::Stormcloak); }
    inline bool ConcordatPair(Side a, Side b) { return Pair(a, b, Side::Imperial, Side::Thalmor); }

    // May the simulation open a war between these two now?
    //  - The Empire and the Stormcloaks: while the game's war is undecided, yes (their campaigns are that war). Under
    //    the truce, no. Once the game has decided it, never again: it is over.
    //  - The Empire and the Thalmor: the White-Gold Concordat binds them while the Empire still fights for Skyrim.
    //    Only once the civil war is decided may that peace break.
    inline bool WarAllowed(Side a, Side b, State s) {
        if (CivilWarPair(a, b)) return s == State::Undecided;
        if (ConcordatPair(a, b)) return s == State::ImperialsWon || s == State::StormcloaksWon;
        return true;
    }

    // May a war between them that the simulation runs go on (battles, morale)?
    inline bool WarGoesOn(Side a, Side b, State s) { return !CivilWarPair(a, b) || s == State::Undecided; }

    // May the simulation name a winner of a war between them? Not of the civil war: the game decides that one. A
    // campaign of the simulation may end; nobody has "won the war" or "surrendered" by it.
    inline bool MayNameWinner(Side a, Side b, State s) { return !CivilWarPair(a, b) || s == State::ImperialsWon || s == State::StormcloaksWon; }

    // The winner the game named, as a side; None while it has named none.
    inline Side Winner(State s) { return s == State::ImperialsWon ? Side::Imperial : s == State::StormcloaksWon ? Side::Stormcloak : Side::None; }

    // The band the pair's relation score is held in.
    struct Band {
        int lo;
        int hi;
    };
    // warThreshold: the score at or below which the simulation calls a pair "at war" (the higher of the two factions').
    inline Band BandFor(Side a, Side b, State s, int warThreshold, int lowest, int highest) {
        Band band{lowest, highest};
        // Enemies as long as both exist: no friendship, trade pact or alliance between the Empire and the Stormcloaks.
        if (CivilWarPair(a, b)) band.hi = std::min(highest, -20);
        // Bound by treaty while the civil war is undecided: never so bad that it reads as war.
        if (ConcordatPair(a, b) && !WarAllowed(a, b, s)) band.lo = std::max(lowest, warThreshold + 5);
        return band;
    }

    // Where a score outside its band goes back to (unchanged when inside).
    inline int Held(int score, const Band& band) { return std::clamp(score, band.lo, band.hi); }

    // ---- every other faction's own story
    // The user (2026-11-20): "nothing should contradict vanilla quests. And freedom with politics should be taken only
    // after the player has completed all vanilla quests that would otherwise contradict new wars of our politics
    // system." A faction of the simulation that stands for one of the game's own (the Companions, the Thieves Guild,
    // the College, the Dark Brotherhood...) is bound while that faction's questline is unfinished: its leaders, its
    // hall and its enemies are the game's to decide until then. Bound, it goes to no war, nobody of it is killed,
    // seized or deposed, and it is not dissolved; it still trades, quarrels and makes friends.

    // The quests that end a faction's questline in the game, by editor id ("A|B": either ends it). Empty: the game
    // tells no story of its own about it. The Empire and the Stormcloaks are held by the civil war's rules above.
    inline std::vector<std::string> QuestlineOf(std::string engineFaction) {
        for (auto& c : engineFaction) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (engineFaction == "companionsfaction") return {"C06"};                 // Glory of the Dead
        if (engineFaction == "thievesguildfaction") return {"TG09"};              // Darkness Returns
        if (engineFaction == "collegeofwinterholdfaction") return {"MG08"};       // The Eye of Magnus
        if (engineFaction == "darkbrotherhoodfaction") return {"DB11|DBDestroy"}; // Hail Sithis!, or the Brotherhood destroyed
        if (engineFaction == "thalmorfaction") return {"MQ305"};                  // the main quest: its envoys sit at the council
        if (engineFaction == "crimefactionreach") return {"MS02"};                // No One Escapes Cidhna Mine (the Silver-Bloods)
        if (engineFaction == "crimefactionrift") return {"TG09"};                 // the Black-Briars' part in the Guild's story
        if (engineFaction == "dlc1hunterfaction" || engineFaction == "dlc1vampirefaction") return {"DLC1VQ08"};   // Kindred Judgment
        return {};
    }

    // What the game says of one quest.
    enum class Quest : std::uint8_t { NotInGame = 0, Unfinished = 1, Finished = 2 };

    // The quest that still binds a faction: the first of its questline that the game has and that is not finished
    // (of "A|B", only when neither is). Empty: it is free. A quest the load order does not have binds nobody.
    template <class StateOf>
    inline std::string BoundBy(const std::vector<std::string>& questline, StateOf&& stateOf) {
        for (const auto& entry : questline) {
            std::string firstOpen;
            bool finished = false;
            std::size_t from = 0;
            while (from <= entry.size()) {
                const auto bar = entry.find('|', from);
                const auto id = entry.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
                if (!id.empty()) {
                    const Quest q = stateOf(id);
                    if (q == Quest::Finished) finished = true;
                    else if (q == Quest::Unfinished && firstOpen.empty()) firstOpen = id;
                }
                if (bar == std::string::npos) break;
                from = bar + 1;
            }
            if (!finished && !firstOpen.empty()) return firstOpen;
        }
        return {};
    }

    // Said to the political dungeon master about a faction that is bound. `story`: the quest's name in the game.
    inline std::string BoundTelling(const std::string& story) {
        return "Its own story in Skyrim is not finished (" + story + "): it keeps its leaders, its hall and its place. No war by it or against "
               "it, no attempt on anyone of it, nobody of it killed, seized or deposed, and it is not dissolved. Trade, quarrels, favours, "
               "insults and friendships only.";
    }

    // The lowest a bound faction's score with anyone may sink: short of what the simulation calls war.
    inline int BoundFloor(int warThreshold, int lowest) { return std::max(lowest, warThreshold + 5); }

    // Event types that may not touch a bound faction, whoever proposes them.
    inline bool HarmsTheBound(const std::string& eventType) {
        return eventType == "war_declaration" || eventType == "assassination_attempt" || eventType == "surrender" ||
               eventType == "battle_result" || eventType == "battle_scheduled" || eventType == "border_skirmish";
    }

    // Said to the political dungeon master and to the people of Skyrim, as fact. {player} is filled in by the caller.
    inline std::string Telling(State s, int allegiance) {
        std::string text;
        switch (s) {
            case State::Undecided:
                text = "Skyrim's civil war between the Empire and the Stormcloaks is undecided: both sides are in the field, neither has won, "
                       "surrendered or made peace, and nobody may say otherwise. The Empire is bound to the Aldmeri Dominion by the "
                       "White-Gold Concordat and is not at war with the Thalmor.";
                break;
            case State::Truce:
                text = "A truce holds between the Empire and the Stormcloaks, agreed at the peace council on High Hrothgar while the dragons are "
                       "the greater danger: there is no fighting between them now, and neither has won the war. The Empire is bound to the "
                       "Aldmeri Dominion by the White-Gold Concordat and is not at war with the Thalmor.";
                break;
            case State::ImperialsWon:
                text = "The civil war is over and the Empire won it: Windhelm has fallen, Ulfric Stormcloak is defeated, and only scattered "
                       "Stormcloak camps hold out. Nobody speaks of the war as undecided, and nothing says the Stormcloaks won.";
                break;
            case State::StormcloaksWon:
                text = "The civil war is over and the Stormcloaks won it: Solitude has fallen, General Tullius is defeated, and the Legion holds "
                       "no city of Skyrim any more. Nobody speaks of the war as undecided, and nothing says the Empire won.";
                break;
        }
        const bool decided = s == State::ImperialsWon || s == State::StormcloaksWon;
        if (allegiance == 1) text += decided ? " {player} fought in it for the Empire." : " {player} has taken the Empire's side in it.";
        else if (allegiance == 2) text += decided ? " {player} fought in it for the Stormcloaks." : " {player} has taken the Stormcloaks' side in it.";
        return text;
    }
}
