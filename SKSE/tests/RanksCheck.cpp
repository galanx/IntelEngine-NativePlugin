// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/RanksCheck.cpp
#include "Ranks.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::Ranks;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void CallCheck() {
    using namespace IntelEngine::Ranks;
    Require(!Called(0) && Called(1) && Called(5), "whoever holds a rank is called, a stranger is not");
    Require(Caller(0, 0) == 0 && Caller(1, 0) == 1 && Caller(0, 2) == 2, "the side the player has a rank with calls");
    Require(Caller(3, 1) == 1 && Caller(1, 4) == 2, "of two, the one they stand higher with");
    Require(Caller(2, 2) == 0, "equal ranks with both: neither calls, the choice is the player's");
}

int main() {
    CallCheck();
    try {
        // ---- the ladder has six rungs, each higher than the last
        Require(RankOf(0) == 0 && RankOf(9) == 0 && RankOf(10) == 1 && RankOf(29) == 1 && RankOf(30) == 2 && RankOf(70) == 3 && RankOf(130) == 4 &&
                    RankOf(220) == 5 && RankOf(99999) == 5,
                "renown to rank");
        for (int i = 1; i < kRanks; ++i) Require(kNeeded[i] > kNeeded[i - 1], "each rank takes more than the one before");
        Require(ToNext(0) == 10 && ToNext(25) == 5 && ToNext(220) == 0 && ToNext(500) == 0, "what is still to earn");
        Require(Honoured(200, 10, -40) == 4 && Honoured(200, -40, -40) == 0 && Honoured(200, -80, -40) == 0, "no rank with a faction that takes the player for an enemy");

        // ---- a first battle is noticed, and nobody is a captain after two
        Record r;
        Earn(r, Deed::Fought);
        Earn(r, Deed::Won);
        Earn(r, Deed::Kill, 3);
        Require(r.battles == 1 && r.won == 1 && r.kills == 3 && r.renown == 15 && RankOf(r.renown) == 1, "one battle won with three kills: the first rank");
        Earn(r, Deed::Fought);
        Earn(r, Deed::Won);
        Earn(r, Deed::Kill, 40);
        Require(r.kills == 43 && r.renown == 15 + 12 + kKillsCountedABattle && RankOf(r.renown) == 2, "a battle is not a tally sheet: eight kills count, the rest are only remembered");
        int battles = 2;
        while (RankOf(r.renown) < 4) {
            Earn(r, Deed::Fought);
            Earn(r, Deed::Won);
            Earn(r, Deed::Kill, 4);
            ++battles;
        }
        Require(battles >= 7 && battles <= 12, "a captain after some eight to ten battles won, not after three and not after thirty");
        Record lost;
        for (int i = 0; i < 6; ++i) Earn(lost, Deed::Fought);
        Require(lost.won == 0 && RankOf(lost.renown) == 2, "standing in the line counts, won or lost");

        // ---- what lifts and what costs
        Record deeds;
        Earn(deeds, Deed::Champion);
        Earn(deeds, Deed::Saved);
        Earn(deeds, Deed::Quest);
        Earn(deeds, Deed::Captain);
        Require(deeds.champions == 1 && deeds.saved == 1 && deeds.quests == 1 && deeds.captains == 1 && deeds.renown == 12 + 10 + 8 + 6, "a champion, a life saved, an errand, a captain");
        Earn(deeds, Deed::Deserted);
        Require(deeds.deserted == 1 && deeds.renown == 36 - 15, "walking away from a battle costs");
        Earn(deeds, Deed::StruckOwn, 3);
        Require(deeds.renown == 21 - 12, "and so does every one of their own cut down");
        Record none;
        Earn(none, Deed::Deserted);
        Earn(none, Deed::StruckOwn, 5);
        Require(none.renown == 0, "renown does not go below nothing");
        Earn(none, Deed::Kill, 0);
        Earn(none, Deed::Kill, -3);
        Require(none.kills == 0, "nothing earned for nothing done");

        // ---- what a rank is called, by what kind of body the faction is
        Require(KindOf("military") == Kind::Army && KindOf("Guild") == Kind::Guild && KindOf("political_family") == Kind::House && KindOf("noble house") == Kind::House &&
                    KindOf("") == Kind::Army && KindOf("mage college") == Kind::Guild,
                "an army, a guild, a house");
        Require(std::string(Title(Kind::Army, 3)) == "Veteran" && std::string(Title(Kind::Guild, 5)) == "Right Hand" && std::string(Title(Kind::House, 1)) == "Hired Blade",
                "titles");
        Require(std::string(Title(Kind::Army, 0)).empty() && std::string(Title(Kind::Army, 9)).empty() && std::string(Title(Kind::House, -1)).empty(), "no title without a rank");
        for (const Kind kind : {Kind::Army, Kind::Guild, Kind::House}) {
            for (int i = 1; i < kRanks; ++i) {
                Require(Title(kind, i)[0] != '\0', "every rank has a name");
                for (int k = i + 1; k < kRanks; ++k) Require(std::string(Title(kind, i)) != Title(kind, k), "and no two the same");
            }
        }

        // ---- what a rank gives
        Require(Pay(120, 0) == 120 && Pay(120, 2) == 180 && Pay(120, 5) == 270 && Pay(0, 5) == 0 && Pay(120, -1) == 120, "better pay with rank");
        Require(FileSize(0) == 0 && FileSize(1) == 0 && FileSize(2) == 2 && FileSize(3) == 2 && FileSize(4) == 4 && FileSize(5) == 4, "soldiers at their side from the second rank on");
        Require(!GivesTheWord(3) && GivesTheWord(4) && GivesTheWord(5), "the line waits for a captain's word");
        Require(!Famous(2) && Famous(3), "known beyond the faction from Veteran on");

        // ---- how they are spoken of
        Record known;
        Require(KnownAs(known, 0, Kind::Army, "the Imperial Legion").empty(), "nothing is said of somebody who has done nothing");
        Earn(known, Deed::Fought, 4);
        Earn(known, Deed::Won, 3);
        Earn(known, Deed::Champion);
        const std::string said = KnownAs(known, 3, Kind::Army, "the Imperial Legion");
        Require(said == "a Veteran of the Imperial Legion: fought in 4 of their battles (3 won), and cut down 1 champion of their enemies", said.c_str());
        Require(KnownAs(known, 1, Kind::Army, "the Stormcloaks").rfind("an Auxiliary of the Stormcloaks", 0) == 0, "an before a vowel");
        Record once;
        Earn(once, Deed::Fought);
        Require(KnownAs(once, 0, Kind::Army, "the Thalmor") == "someone who has stood with the Thalmor: fought in 1 battle of theirs", "somebody without a rank yet");
        Earn(once, Deed::Deserted);
        Require(KnownAs(once, 0, Kind::Army, "the Thalmor").find("walked away from 1 battle") != std::string::npos, "what they did wrong is known too");

        // ---- kept with the save
        Book book;
        book["ImperialFaction"] = known;
        book["SilverBloodFamily"] = deeds;
        const auto bytes = Serialize(book);
        Book back;
        Require(Deserialize(bytes, back) && back.size() == 2 && back["ImperialFaction"].battles == 4 && back["ImperialFaction"].champions == 1 &&
                    back["SilverBloodFamily"].renown == deeds.renown && back["SilverBloodFamily"].deserted == 1,
                "what is written is what is read");
        auto shortened = bytes;
        shortened.resize(bytes.size() - 5);
        Book kept;
        kept["x"] = known;
        Require(!Deserialize(shortened, kept) && kept.size() == 1, "a record cut short is refused and nothing is lost");
        Require(Deserialize(Serialize(Book{}), kept) && kept.empty(), "an empty book");

        std::cout << "RanksCheck: passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "RanksCheck: FAILED: " << e.what() << "\n";
        return 1;
    }
}
