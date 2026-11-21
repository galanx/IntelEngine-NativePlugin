// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/BattlefieldCheck.cpp
#include "Battlefield.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::Battlefield;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static bool Near(float a, float b, float within = 1.f) { return std::fabs(a - b) <= within; }

int main() {
    try {
        // ---- the compass
        Require(Near(Dir(0.f).y, 1.f, 0.001f) && Near(Dir(90.f).x, 1.f, 0.001f), "north is +Y, east is +X");
        Require(Near(Bearing({0, 0}, {0, 100}), 0.f) && Near(Bearing({0, 0}, {100, 0}), 90.f) && Near(Bearing({0, 0}, {-100, 0}), 270.f), "bearings");
        Require(Near(Apart(350.f, 10.f), 20.f) && Near(Apart(10.f, 190.f), 180.f) && Near(Apart(-90.f, 270.f), 0.f), "the angle between two bearings");

        // ---- never where the player stands, never beyond the loaded ground
        const Vec player{1000.f, -2000.f};
        for (const float facing : {0.f, 77.f, 180.f, 291.f}) {
            for (const bool hasPlace : {false, true}) {
                for (const Vec place : {Vec{1000.f, 3000.f}, Vec{-4000.f, -2500.f}, Vec{1200.f, -1900.f}, Vec{7300.f, -2000.f}}) {
                    const auto tries = Candidates(player, facing, hasPlace, place);
                    Require(tries.size() >= 12, "enough places to try");
                    for (const auto& c : tries) {
                        const float d = Dist(player, c.centre);
                        Require(d >= kNearOutOfView - 1.f && d <= kFar + 1.f, "between the nearest and the farthest");
                        Require(c.inView == (Apart(Bearing(player, c.centre), facing) <= kViewHalfAngle + 0.01f), "in view is said truly");
                        Require(!c.inView || d >= kNearInView - 1.f, "farther off where the player is looking");
                        // The lines stand across the player's line of sight.
                        Require(Near(Apart(c.axis, Bearing(player, c.centre)), 90.f, 0.1f), "the lines stand to the left and the right");
                        const Field f = Draw(c.centre, c.axis, WaveSize(1), WaveSize(1));
                        for (const auto* line : {&f.a, &f.b}) {
                            for (const Vec& spot : line->spots) Require(Dist(player, spot) >= kNearOutOfView - 350.f, "no soldier near the player");      // the files run toward the player: half a rank nearer than the middle
                            const auto reserves = ReservePoints(*line, player, facing);
                            Require(!reserves.empty(), "somewhere to come from");
                            for (const Vec& reserve : reserves) {
                                Require(Dist(player, reserve) >= kReserveClear, "nobody arrives next to the player");
                                Require(Dist(player, reserve) <= 7600.f, "and nobody from beyond the loaded ground");
                            }
                        }
                    }
                }
            }
        }

        // ---- a battle named after a place is fought on the way there, short of it
        {
            const Vec place{1000.f, 4300.f};                    // 6,300 north of the player, who looks north
            const auto tries = Candidates(player, 0.f, true, place);
            Require(Near(Bearing(player, tries[0].centre), 0.f, 0.1f), "the first place tried lies on the way there");
            Require(Near(Dist(tries[0].centre, place), kShortOfPlace, 1.f), "and short of the place itself");
            const Vec close{1000.f, 500.f};                     // the player is 2,500 from the place already
            const auto near = Candidates(player, 0.f, true, close);
            Require(Dist(player, near[0].centre) >= kNearInView - 1.f, "still not drawn up next to the player");
            // Looking away from the place: the same spot may be nearer, the player does not see them come.
            const auto behind = Candidates(player, 180.f, true, close);
            Require(!behind[0].inView && Near(Dist(player, behind[0].centre), kNearOutOfView, 1.f), "behind the player they may be nearer");
        }

        // ---- how they stand
        {
            const Field f = Draw({0.f, 0.f}, 90.f, 8, 5);       // A in the west looking east, B in the east looking west
            Require(Near(Dist(f.a.centre, f.b.centre), kLineGap) && f.a.centre.x < 0.f && f.b.centre.x > 0.f, "two lines, the field between them");
            Require(Near(f.a.facing, 90.f) && Near(f.b.facing, 270.f), "each looks at the other");
            Require(f.a.spots.size() == 8 && f.b.spots.size() == 5, "a spot for everyone");
            Require(Dist(f.a.spots[0], f.b.centre) < Dist(f.a.centre, f.b.centre), "the commander stands in front of the line");
            Require(Near(Dist(f.a.spots[0], f.a.centre), kCommanderAhead), "by this much");
            for (const auto* line : {&f.a, &f.b}) {
                for (std::size_t i = 0; i < line->spots.size(); ++i) {
                    for (std::size_t k = i + 1; k < line->spots.size(); ++k) {
                        Require(Dist(line->spots[i], line->spots[k]) >= 100.f, "nobody stands in somebody else");
                    }
                }
            }
            for (const Vec& spot : f.a.spots) Require(spot.x < 0.f, "side A on its own half");
            for (const Vec& spot : f.b.spots) Require(spot.x > 0.f, "side B on its own half");
            // Seven in the ranks: six in front, one behind them.
            int front = 0, second = 0;
            for (std::size_t i = 1; i < f.a.spots.size(); ++i) {
                if (Near(f.a.spots[i].x, f.a.centre.x)) ++front;
                else if (Near(f.a.spots[i].x, f.a.centre.x - kRank)) ++second;
            }
            Require(front == kFront && second == 1, "a front rank of six, the rest behind it");
            Require(Draw({0.f, 0.f}, 0.f, 0, 1).a.spots.empty() && Draw({0.f, 0.f}, 0.f, 1, 1).a.spots.size() == 1, "no soldiers, no spots; one soldier is the commander");
        }

        // ---- those who come later
        {
            const Field f = Draw({0.f, 0.f}, 90.f, 8, 8);
            const Vec far{0.f, -5000.f};                        // watching from the south
            const Vec reserveA = ReservePoints(f.a, far, 0.f).front();
            Require(reserveA.x < f.a.centre.x - kReserveDepth + 1.f, "from behind their own line");
            // The player stands in side A's line: its reinforcements still do not appear beside them.
            const Vec inLine{f.a.centre.x - 300.f, 0.f};
            const auto fromLine = ReservePoints(f.a, inLine, 90.f);
            Require(!fromLine.empty(), "somewhere to come from for a side the player stands in");
            for (const Vec& at : fromLine) Require(Dist(inLine, at) >= kReserveClear, "not next to a player who stands in the line");
            // Looking at the enemy from their own line: what comes up behind them comes up behind their back.
            Require(Apart(Bearing(inLine, fromLine.front()), 90.f) > kViewHalfAngle, "and first from where they are not looking");
            // The player stands where they would come from, looking back at it: another place is taken.
            const Vec atReserve = Along(f.a.centre, f.a.facing, -kReserveDepth);
            const auto fromBehind = ReservePoints(f.a, atReserve, 270.f);
            Require(!fromBehind.empty(), "somewhere to come from when the player waits behind the line");
            for (const Vec& at : fromBehind) Require(Dist(atReserve, at) >= kReserveClear, "nor on top of a player who waits behind it");
            Require(Dist(ReserveSpot(reserveA, 90.f, 0), ReserveSpot(reserveA, 90.f, 1)) >= 100.f &&
                    Dist(ReserveSpot(reserveA, 90.f, 0), ReserveSpot(reserveA, 90.f, 3)) >= 100.f, "they arrive side by side, not in one spot");
        }

        // ---- how many
        {
            int all = 0;
            for (int wave = 1; wave <= kWaves; ++wave) {
                Require(WaveSize(wave) > 0, "every wave brings somebody");
                all += WaveSize(wave);
            }
            Require(all == kMaxPerSide && WaveSize(0) == 0 && WaveSize(kWaves + 1) == 0, "twenty-two a side in all, and no more waves than there are");
            Require(WaveSize(1) > WaveSize(2), "the lines that draw up are the bigger part");
            // Nobody is drawn up in water, a ford included; a puddle's depth or no water at all is dry ground.
            Require(Wet(-4000.f, -4025.f) && Wet(-4000.f, -4060.f), "a ford is water to stand in");
            Require(!Wet(-4000.f, -3990.f) && !Wet(-4000.f, -4005.f), "ground above the water, or a hand's breadth under it, is dry");
            Require(!Wet(3.0e38f, 0.f), "where there is no water nothing is wet");
            // A champion is more than a soldier and less than an army.
            Require(kChampionForce > kBlowForce * 1.5f && kChampionForce <= kBlowForce * 2.f, "a champion strikes harder than a soldier, not three times as hard");
            Require(kChampionHardiness > kHardiness, "and stands more");
            // Every soldier's spot can be asked: a line of eight is the commander and seven behind him, none of them twice.
            const Line line = DrawLine({0.f, 0.f}, 0.f, 8);
            Require(line.spots.size() == 8, "a spot for each soldier of the line");
        }

        // ---- how it is decided
        {
            // An army breaks when it has lost about two men in three, sooner without its commander.
            int morale = 100, lost = 0;
            while (morale > kBreakAt) {
                morale -= kLossSoldier;
                ++lost;
            }
            Require(lost >= 12 && lost <= 16, "an army breaks before the last man, not after the first few");
            Require((100 - kLossCommander - kBreakAt) / kLossSoldier < lost, "sooner when the commander falls");
            Require(Verdict(8, 3, 60, 40) == 1 && Verdict(2, 6, 70, 50) == 2, "the many have the better of the few");
            Require(Verdict(5, 4, 60, 55) == 0 && Verdict(0, 0, 50, 50) == 0, "an even field is nobody's");
            Require(Verdict(4, 4, 80, 40) == 1 && Verdict(3, 4, 30, 70) == 2, "even numbers: the stouter heart");
            Require(Verdict(3, 1, 50, 50) == 1 && Verdict(3, 2, 50, 50) == 0, "one man more is not yet the better of it");
            // Both broke in one look: it was fought out, so any lead keeps the field.
            Require(BrokeTogether(7, 5, 25, 25) == 1 && BrokeTogether(4, 6, 25, 20) == 2, "both broke: the more men standing keep the field");
            Require(BrokeTogether(6, 6, 25, 20) == 1 && BrokeTogether(6, 6, 15, 20) == 2, "as many each: the stouter heart");
            Require(BrokeTogether(6, 6, 25, 25) == 0 && BrokeTogether(0, 0, 0, 0) == 0, "the same in both: a drawn battle, and a rare one");
            // The player's coming is worth something to a side that is still whole.
            Require(kHeartMost == kHeartFull + kPlayerJoins && kHeartMost > kHeartFull, "a full heart can still be lifted by who joins it");
            {
                int joined = kHeartMost, other = kHeartFull - kPlayerJoinsEnemy, fell = 0;
                while (other > kBreakAt) {
                    joined -= kLossSoldier;
                    other -= kLossSoldier;
                    ++fell;
                }
                Require(joined > kBreakAt && fell > 0, "man for man, the side the player joined outlasts the other");
            }
        }

        // ---- where an army goes when it is over
        {
            const Vec field{0.f, 0.f};
            const Vec north{0.f, 1.f}, south{0.f, -1.f};
            // Home far to the north, a village close by, a town to the south, another far to the east.
            const std::vector<Town> towns = {{{0.f, 60000.f}, 1, true}, {{3000.f, 2000.f}, 2, false}, {{0.f, -30000.f}, 3, false}, {{50000.f, 5000.f}, 4, false}};
            Require(MarchTo(towns, field, north, false, 0) == 0, "the winners march home, however far");
            Require(MarchTo(towns, field, north, true, 0) == 0, "the beaten run home when home lies behind them");
            Require(MarchTo(towns, field, south, true, 0) == 2, "the beaten never run home through the army that beat them: the nearest town on their own side");
            Require(MarchTo(towns, field, south, false, 0) == 0, "the winners may: the field is theirs");
            Require(MarchTo(towns, field, north, false, 1) == 2, "a town the other army is going to is left to them");
            Require(MarchTo({{{3000.f, 2000.f}, 2, true}}, field, north, false, 0) == -1, "a home in sight is nowhere to march off to");
            Require(MarchTo({}, field, north, false, 0) == -1 && MarchTo(towns, field, {0.f, 0.f}, true, 0) == 0, "no towns: none; no direction known: home");
            // With every town on the far side of the enemy the beaten still go somewhere: the nearest.
            Require(MarchTo({{{0.f, 60000.f}, 1, true}, {{0.f, 20000.f}, 5, false}}, field, south, true, 0) == 1, "cut off from everything: the nearest town, home or not");
        }

        // ---- what a battle has in store
        {
            int champions = 0, both = 0, flanks = 0, turncoats = 0, plain = 0;
            const int battles = 4000;
            for (std::uint32_t seed = 1; seed <= static_cast<std::uint32_t>(battles); ++seed) {
                const Twists t = Plan(seed, false);
                const Twists again = Plan(seed, false);
                Require(t.champion == again.champion && t.flank == again.flank && t.turncoat == again.turncoat, "a battle plays out one way");
                Require(!(t.champion != 0 && t.flank != 0 && t.turncoat != 0), "never everything at once");
                Require(t.flank == 0 || (t.flankAfter >= 30.f && t.flankAfter < 70.f), "the hidden force waits half a minute and more");
                Require(t.championAfter >= 40.f && t.championAfter < 75.f, "a champion comes up when the fighting has gone on a while");
                if (t.champion != 0) ++champions;
                if (t.champion == 3) ++both;
                if (t.flank != 0) ++flanks;
                if (t.turncoat != 0) ++turncoats;
                if (t.champion == 0 && t.flank == 0 && t.turncoat == 0) ++plain;
                const Twists s = Plan(seed, true);
                Require(s.champion == 0 && s.flank == 0 && s.turncoat == 0, "a skirmish is what it looks like");
            }
            Require(champions > battles * 55 / 100 && champions < battles * 75 / 100, "a champion in about two battles in three");
            Require(both > battles * 5 / 100 && both < battles * 18 / 100, "now and then one on each side");
            Require(flanks > battles * 24 / 100 && flanks < battles * 36 / 100, "a hidden force in about one battle in three");
            Require(turncoats > battles * 11 / 100 && turncoats < battles * 21 / 100, "treachery in about one in six");
            Require(plain > battles * 12 / 100 && plain < battles * 35 / 100, "and some battles are only what they look like");
            Require(Turncoats(9) == 3 && Turncoats(6) == 2 && Turncoats(3) == 1 && Turncoats(2) == 0 && Turncoats(20) == kTurncoatsMost && Turncoats(0) == 0,
                    "a third change sides, three at most, and somebody stays");

            const Field f = Draw({0.f, 0.f}, 90.f, 8, 8);
            const Vec watcher{0.f, -5000.f};
            const auto points = FlankPoints(f.b, watcher, 0.f);         // side A's hidden force falls on line B
            Require(!points.empty(), "somewhere to break cover from");
            for (const Vec& at : points) {
                Require(Dist(watcher, at) >= kReserveClear && Dist(watcher, at) <= 7600.f, "not next to the player, not off the loaded ground");
                Require(std::fabs(at.y - f.b.centre.y) >= 2000.f, "out to the side of the line it falls on");
            }
            // The player stands in line B: no point of the flank is beside them.
            const Vec inLineB{f.b.centre.x, 200.f};
            for (const Vec& at : FlankPoints(f.b, inLineB, 270.f)) Require(Dist(inLineB, at) >= kReserveClear, "nor beside a player who stands in that line");
        }

        // ---- the player
        Require(WouldHave(0) && WouldHave(kHostileStanding + 1) && !WouldHave(kHostileStanding) && !WouldHave(-100), "a side has anyone it does not take for an enemy");
        Require(Pay(false, 9) == 0 && Pay(true, 0) == 120 && Pay(true, 3) == 300 && Pay(true, 40) == 600 && Pay(true, -2) == 120, "the pay of one who fought and won");
        Require(kTriggerDistance - kShortOfPlace <= kFar, "a battle that draws up as the player comes near stands on loaded ground");

        std::cout << "BattlefieldCheck: passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "BattlefieldCheck: FAILED: " << e.what() << "\n";
        return 1;
    }
}
