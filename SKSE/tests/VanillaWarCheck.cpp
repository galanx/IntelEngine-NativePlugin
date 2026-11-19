// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/VanillaWarCheck.cpp
#include "VanillaWar.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::VanillaWar;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        // ---- what the game says
        Require(Of({}) == State::Undecided, "a new game: undecided");
        Require(Of({false, 1, false, false}) == State::Undecided, "joined a side: still undecided");
        Require(Of({false, 2, true, false}) == State::Truce, "the peace council held: a truce");
        Require(Of({false, 2, true, true}) == State::Undecided, "the main quest over: the truce no longer holds");
        Require(Of({true, 1, false, false}) == State::ImperialsWon, "finished on the Empire's side");
        Require(Of({true, 2, true, false}) == State::StormcloaksWon, "finished on the Stormcloaks' side, council or not");

        // ---- who is who, by the game faction a faction of the simulation stands for
        Require(SideOf("CWImperialFaction") == Side::Imperial && SideOf("cwsonsfaction") == Side::Stormcloak, "the two sides");
        Require(SideOf("ThalmorFaction") == Side::Thalmor && SideOf("CompanionsFaction") == Side::None && SideOf("") == Side::None, "and the rest");

        const Side I = Side::Imperial, S = Side::Stormcloak, T = Side::Thalmor, N = Side::None;
        // ---- wars the simulation may open
        Require(WarAllowed(I, S, State::Undecided) && WarAllowed(S, I, State::Undecided), "the civil war is fought while undecided");
        Require(!WarAllowed(I, S, State::Truce), "not under the truce");
        Require(!WarAllowed(I, S, State::ImperialsWon) && !WarAllowed(S, I, State::StormcloaksWon), "and never again once it is decided");
        Require(!WarAllowed(I, T, State::Undecided) && !WarAllowed(T, I, State::Truce), "the Concordat binds the Empire while it fights for Skyrim");
        Require(WarAllowed(I, T, State::ImperialsWon) && WarAllowed(T, I, State::StormcloaksWon), "only afterwards may that peace break");
        Require(WarAllowed(S, T, State::Undecided) && WarAllowed(N, I, State::Truce) && WarAllowed(N, N, State::Undecided), "nobody else is bound by it");

        // ---- wars that go on, and who may be called the winner
        Require(WarGoesOn(I, S, State::Undecided) && !WarGoesOn(I, S, State::Truce) && !WarGoesOn(S, I, State::ImperialsWon), "fighting stops with the truce and the end");
        Require(WarGoesOn(S, T, State::Truce), "other wars are their own");
        Require(!MayNameWinner(I, S, State::Undecided) && !MayNameWinner(S, I, State::Truce), "nobody wins the civil war before the game says so");
        Require(MayNameWinner(I, S, State::ImperialsWon) && MayNameWinner(N, T, State::Undecided), "afterwards, and in other wars, a winner may be named");
        Require(Winner(State::ImperialsWon) == I && Winner(State::StormcloaksWon) == S && Winner(State::Truce) == N && Winner(State::Undecided) == N, "the winner the game named");

        // ---- the band a score is held in (war at -49 or below here)
        const auto cw = BandFor(I, S, State::Undecided, -49, -100, 100);
        Require(cw.lo == -100 && cw.hi == -20 && Held(35, cw) == -20 && Held(-70, cw) == -70, "the Empire and the Stormcloaks never become friends");
        const auto bound = BandFor(T, I, State::Undecided, -49, -100, 100);
        Require(bound.lo == -44 && bound.hi == 100 && Held(-90, bound) == -44 && Held(15, bound) == 15, "the Empire and the Thalmor stay short of war");
        const auto freed = BandFor(I, T, State::StormcloaksWon, -49, -100, 100);
        Require(freed.lo == -100 && Held(-90, freed) == -90, "until the civil war is decided");
        const auto others = BandFor(S, T, State::Undecided, -49, -100, 100);
        Require(others.lo == -100 && others.hi == 100, "others are not held");

        // ---- what is told
        Require(Telling(State::Undecided, 0).find("undecided") != std::string::npos && Telling(State::Undecided, 0).find("{player}") == std::string::npos, "undecided, nobody's side");
        Require(Telling(State::Undecided, 2).find("{player} has taken the Stormcloaks' side") != std::string::npos, "a side taken in a war still fought");
        Require(Telling(State::Truce, 0).find("truce") != std::string::npos, "the truce");
        Require(Telling(State::ImperialsWon, 1).find("the Empire won") != std::string::npos && Telling(State::ImperialsWon, 1).find("{player} fought in it for the Empire") != std::string::npos, "won, and fought for");
        Require(Telling(State::StormcloaksWon, 0).find("the Stormcloaks won") != std::string::npos, "the other ending");
    } catch (const std::exception& e) {
        std::cerr << "VanillaWarCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "VanillaWarCheck passed\n";
    return 0;
}
