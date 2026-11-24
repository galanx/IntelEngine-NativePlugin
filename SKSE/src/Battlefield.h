#pragma once

// Where a battle is fought and how it stands there. Pure: no game types, so it runs in a plain check
// (tests/BattlefieldCheck.cpp). The game side (BattleManager) asks this for the places worth trying, tests each
// against the world (ground, water, houses, people), and draws the armies up on the first one that holds.
//
// What this replaced (3.8, watched 2026-11-20): both armies were created on the spot the player stood on, 200 units
// apart and fighting from the first frame; reinforcements appeared at the player's feet; a battle "at" a town was
// fought in its streets, and one whose hour had come was fought wherever the player happened to be. Nobody was asked
// which side they were on, and nobody led either army.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace IntelEngine::Battlefield {

    struct Vec {
        float x = 0.f;
        float y = 0.f;
    };

    // ---- the game's compass: 0 is north (+Y), angles grow clockwise, in degrees

    inline constexpr float kPi = 3.14159265358979f;
    inline float Norm(float deg) {
        deg = std::fmod(deg, 360.f);
        return deg < 0.f ? deg + 360.f : deg;
    }
    inline Vec Dir(float deg) {
        const float rad = deg * kPi / 180.f;
        return {std::sin(rad), std::cos(rad)};
    }
    inline float Bearing(Vec from, Vec to) { return Norm(std::atan2(to.x - from.x, to.y - from.y) * 180.f / kPi); }
    inline float Dist(Vec a, Vec b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
    inline Vec Along(Vec from, float deg, float distance) {
        const Vec d = Dir(deg);
        return {from.x + d.x * distance, from.y + d.y * distance};
    }
    // The angle between two bearings, 0 to 180.
    inline float Apart(float a, float b) {
        const float d = std::fabs(Norm(a) - Norm(b));
        return d > 180.f ? 360.f - d : d;
    }

    // ---- where the armies may be drawn up

    // Never where the player stands. Where the player is looking they are farther off, at the distance the game
    // itself lets people fade into view; behind or beside the player they may be nearer.
    inline constexpr float kNearInView = 4500.f;
    inline constexpr float kNearOutOfView = 3200.f;
    // And never beyond the ground that is loaded around the player (five cells of 4,096 across, the player's in the
    // middle: 8,192 at the least). The farthest point used is a reserve point of a field at kFar: about 6,300.
    inline constexpr float kFar = 5600.f;
    // Half of what the player sees, generously: a screen three monitors wide shows well over a right angle.
    inline constexpr float kViewHalfAngle = 80.f;
    // A battle named after a place is fought this far short of it, on the player's side: outside the town.
    inline constexpr float kShortOfPlace = 1500.f;
    // The player comes within this of the place a battle is to be fought at: the armies draw up.
    inline constexpr float kTriggerDistance = 6500.f;
    // No field where people live or stand: nobody who is not of the battle within this of its middle.
    inline constexpr float kClearOfPeople = 1900.f;
    // Nor a door into a house, a cave or a town within this.
    inline constexpr float kClearOfHouses = 1500.f;
    // The ground between the two lines may rise or fall this much, and no more: not across a cliff.
    inline constexpr float kMaxRise = 450.f;

    struct Candidate {
        Vec centre;
        float axis = 0.f;       // the bearing from line A to line B
        bool inView = false;    // the player is looking that way
    };

    // The places worth trying, best first. With a place the battle is named after, the first lies on the way there;
    // the others fan out round the player. The lines always stand across the player's line of sight: one to the left,
    // one to the right, so neither has its back to the player and neither charges through them.
    inline std::vector<Candidate> Candidates(Vec player, float facing, bool hasPlace, Vec place) {
        std::vector<Candidate> out;
        const float toward = hasPlace && Dist(player, place) > 1.f ? Bearing(player, place) : Norm(facing);
        const auto add = [&](float bearing, float distance) {
            const bool inView = Apart(bearing, facing) <= kViewHalfAngle;
            const float nearest = inView ? kNearInView : kNearOutOfView;
            distance = std::clamp(distance, nearest, kFar);
            for (const auto& have : out) {
                if (Dist(have.centre, Along(player, bearing, distance)) < 200.f) return;      // the same place twice
            }
            out.push_back({Along(player, bearing, distance), Norm(bearing + 90.f), inView});
        };
        if (hasPlace) add(toward, Dist(player, place) - kShortOfPlace);
        for (const float turn : {0.f, 40.f, -40.f, 80.f, -80.f, 125.f, -125.f, 180.f}) {
            const float bearing = Norm(toward + turn);
            add(bearing, 4700.f);
            add(bearing, 3400.f);
            add(bearing, 5400.f);
        }
        return out;
    }

    // ---- how they stand

    inline constexpr float kLineGap = 1400.f;           // from one front rank to the other
    // Water this far above the ground is water to stand in: no line is drawn up there. (It was sixty: a ford is
    // shallower than that, and the Legion stood knee-deep under the bridge at Honningbrew, 2026-11-21.)
    inline constexpr float kWetAbove = 12.f;
    inline constexpr bool Wet(float water, float ground) { return water > ground + kWetAbove && water < 100000.f; }
    inline constexpr float kFile = 115.f;               // shoulder to shoulder
    inline constexpr float kRank = 150.f;               // one rank behind the other
    inline constexpr int kFront = 6;                    // soldiers in a rank
    inline constexpr float kCommanderAhead = 170.f;     // the commander stands in front of the line
    inline constexpr float kReserveDepth = 1700.f;      // those who come later come from this far behind their line
    inline constexpr float kReserveClear = 2200.f;      // and never from nearer to the player than this

    struct Line {
        Vec centre;                 // the middle of the front rank
        float facing = 0.f;         // the way the line looks: at the other line
        std::vector<Vec> spots;     // one for each soldier; the commander's is the first
    };

    struct Field {
        Vec centre;
        float axis = 0.f;
        Line a;
        Line b;
    };

    inline Line DrawLine(Vec centre, float facing, int soldiers) {
        Line line;
        line.centre = centre;
        line.facing = Norm(facing);
        if (soldiers <= 0) return line;
        line.spots.push_back(Along(centre, facing, kCommanderAhead));
        const int ranked = soldiers - 1;
        for (int k = 0; k < ranked; ++k) {
            const int rank = k / kFront;
            const int file = k % kFront;
            const int inRank = (std::min)(kFront, ranked - rank * kFront);
            const float aside = (static_cast<float>(file) - static_cast<float>(inRank - 1) / 2.f) * kFile;
            line.spots.push_back(Along(Along(centre, facing + 90.f, aside), facing, -static_cast<float>(rank) * kRank));
        }
        return line;
    }

    inline Field Draw(Vec centre, float axis, int soldiersA, int soldiersB) {
        Field field;
        field.centre = centre;
        field.axis = Norm(axis);
        field.a = DrawLine(Along(centre, axis, -kLineGap / 2.f), axis, soldiersA);
        field.b = DrawLine(Along(centre, axis + 180.f, -kLineGap / 2.f), axis + 180.f, soldiersB);
        return field;
    }

    // Where a side's reinforcements may come from, best first: behind their own line or wide of it, and of those
    // places first the ones the player is least likely to see them appear at. Never close to the player, never in
    // front of the player's eyes unless far off, never beyond the ground that is surely loaded. The game side takes
    // the first that can be walked from; none: nobody comes this time.
    inline std::vector<Vec> ReservePoints(const Line& line, Vec player, float playerFacing) {
        const Vec back = Along(line.centre, line.facing, -kReserveDepth);
        const Vec tries[] = {back,
                             Along(back, line.facing + 90.f, 900.f),
                             Along(back, line.facing - 90.f, 900.f),
                             Along(line.centre, line.facing, -(kReserveDepth + 700.f)),
                             Along(back, line.facing + 90.f, 1900.f),
                             Along(back, line.facing - 90.f, 1900.f),
                             Along(line.centre, line.facing, -(kReserveDepth + 1700.f)),
                             Along(back, line.facing + 90.f, 2900.f),
                             Along(back, line.facing - 90.f, 2900.f),
                             Along(line.centre, line.facing + 90.f, 2600.f),
                             Along(line.centre, line.facing - 90.f, 2600.f),
                             Along(line.centre, line.facing, -900.f)};
        struct Scored {
            Vec at;
            float score;
        };
        std::vector<Scored> scored;
        for (const Vec& at : tries) {
            const float distance = Dist(player, at);
            if (distance < kReserveClear || distance > 7600.f) continue;
            const bool inView = Apart(Bearing(player, at), playerFacing) <= kViewHalfAngle;
            float score = (std::min)(distance, 5000.f);
            if (inView && distance < kNearInView) score -= 3000.f;
            scored.push_back({at, score});
        }
        std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) { return a.score > b.score; });
        std::vector<Vec> out;
        for (const auto& s : scored) out.push_back(s.at);
        return out;
    }

    // The spot of one of a group that arrives together (three abreast, facing the field).
    inline Vec ReserveSpot(Vec point, float facing, int index) {
        const int rank = index / 3;
        const int file = index % 3;
        return Along(Along(point, facing + 90.f, (static_cast<float>(file) - 1.f) * kFile), facing, -static_cast<float>(rank) * kRank);
    }

    // ---- how many

    inline constexpr int kWaves = 5;
    inline constexpr int kMaxPerSide = 22;
    // The lines that draw up, and those who come to them as the fighting goes on.
    inline constexpr int WaveSize(int wave) {
        switch (wave) {
            case 1: return 8;
            case 2: return 5;
            case 3: return 4;
            case 4: return 3;
            case 5: return 2;
            default: return 0;
        }
    }

    // ---- how it is decided

    // An army's heart, 100 at the start. Every soldier lost costs some of it, the commander more; at kBreakAt the
    // army breaks and runs. (3.8: two points a soldier and an end at fifteen. Forty-three had to die before an army
    // of twenty-two broke, so none ever did: every battle was fought to the last man.)
    inline constexpr int kLossSoldier = 5;
    inline constexpr int kLossCommander = 15;
    inline constexpr int kBreakAt = 25;
    inline constexpr int kPlayerJoins = 10;         // what the player's coming is worth to a side
    inline constexpr int kPlayerJoinsEnemy = 5;     // and costs the other
    // A heart can be fuller than full by what the player's coming is worth. (It was capped at 100: whoever took a
    // side while the lines still stood, which is when one is asked, gave that side nothing. Played 2026-11-20: the
    // player's side and the other broke in the same moment, at 25 each.)
    inline constexpr int kHeartFull = 100;
    inline constexpr int kHeartMost = kHeartFull + kPlayerJoins;
    // Soldiers of a battle strike each other, and the player, with this much of their usual force. At full force two
    // lines of the game's soldiers cut each other down in a minute (watched 2026-11-20: forty-four people, sixty
    // seconds); a battle should last long enough to be walked into, fought in and turned.
    inline constexpr float kBlowForce = 0.35f;
    // And they stand this much more than they otherwise would.
    inline constexpr float kHardiness = 1.75f;

    // A battle that is not fought out (the player left, or it went on for too long): who had the better of it.
    // 1: side A, 2: side B, 0: neither.
    inline int Verdict(int aliveA, int aliveB, int moraleA, int moraleB) {
        if (aliveA >= aliveB + 2 && aliveA * 2 >= aliveB * 3) return 1;
        if (aliveB >= aliveA + 2 && aliveB * 2 >= aliveA * 3) return 2;
        if (moraleA >= moraleB + 25) return 1;
        if (moraleB >= moraleA + 25) return 2;
        return 0;
    }

    // Both armies broke between one look and the next (the looks are three seconds apart, and two lines that wear
    // each other down evenly lose heart evenly). That battle was fought out: whoever has more men standing keeps the
    // field, with as many each the stouter heart, and only with both the same is it nobody's. (It went by Verdict,
    // which wants a clear lead: seven standing against five was called a standstill, and both walked off.)
    inline constexpr int BrokeTogether(int aliveA, int aliveB, int moraleA, int moraleB) {
        if (aliveA != aliveB) return aliveA > aliveB ? 1 : 2;
        if (moraleA != moraleB) return moraleA > moraleB ? 1 : 2;
        return 0;
    }

    // ---- what a battle has in store

    // Not every battle is two lines wearing each other down. Some armies bring a champion; some keep a second force
    // out of sight on the flank; in some, men turn their coats when it goes badly. Which, is settled when the armies
    // gather (from the battle's own number, so a save plays it one way) and is nobody's secret for ever: a champion
    // is seen coming, the hidden force breaks cover with a shout, and whoever listens to the talk before a battle may
    // have heard of either. A skirmish is what it looks like.
    struct Twists {
        int champion = 0;           // which side fields a champion: 0 none, 1 side A, 2 side B, 3 both
        int flank = 0;              // which side has a second force hidden on the flank
        int turncoat = 0;           // which side some men will desert, to fight for the other
        float flankAfter = 0.f;     // seconds of fighting before the hidden force breaks cover
        float championAfter = 0.f;  // seconds of fighting before a champion comes up
    };

    inline constexpr std::uint32_t Scramble(std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    inline Twists Plan(std::uint32_t seed, bool skirmish) {
        Twists t;
        if (skirmish) return t;
        const std::uint32_t a = Scramble(seed ^ 0x51ED270Bu), b = Scramble(seed ^ 0xC2B2AE35u), c = Scramble(seed ^ 0x27D4EB2Fu);
        const std::uint32_t champion = a % 100u;
        t.champion = champion < 28u ? 1 : champion < 56u ? 2 : champion < 68u ? 3 : 0;
        t.championAfter = 40.f + static_cast<float>((a >> 12) % 35u);
        if (b % 100u < 30u) {
            t.flank = ((b >> 8) & 1u) ? 1 : 2;
            t.flankAfter = 30.f + static_cast<float>((b >> 12) % 40u);
        }
        if (c % 100u < 16u) t.turncoat = ((c >> 8) & 1u) ? 1 : 2;
        // Not everything at once: where there is a hidden force and treachery too, nobody brings a champion besides.
        if (t.flank != 0 && t.turncoat != 0) t.champion = 0;
        return t;
    }

    inline constexpr int kFlankSize = 4;                // the hidden force
    inline constexpr int kTurncoatsMost = 3;            // who change sides, at most
    inline constexpr int kTurnAtMorale = 70;            // they do it when their side's heart has sunk to this
    inline constexpr int kLossChampion = 20;            // what a champion's fall costs their side
    inline constexpr int kLossBetrayed = 10;            // and what being betrayed costs
    // A champion stands three times what a soldier does and strikes harder (kChampionForce). They come up by themselves when
    // the fighting has gone on for a while (Twists::championAfter). (First they came with their side's second wave:
    // a side that was winning never called its second wave, so the champion of the stronger army never came.)
    inline constexpr float kChampionHardiness = 3.0f;
    // What a champion strikes with: nearly twice a soldier's force (kBlowForce), not the game's full force. (At full
    // force, close on three times a soldier's, one champion decided a battle by himself: played 2026-11-21, twelve of
    // the Legion cut down in the minute after he came up, none of his own side lost, over in sixty-nine seconds.)
    inline constexpr float kChampionForce = 0.6f;
    inline constexpr int kChampionPay = 200;            // what the winning side pays for an enemy champion cut down
    // How many of a side's living turn their coats: a third of them, three at most, and never so many that none is left.
    inline constexpr int Turncoats(int alive) {
        const int third = alive / 3;
        const int most = third > kTurncoatsMost ? kTurncoatsMost : third;
        return alive - most < 2 ? 0 : most;
    }

    // Where a hidden force breaks cover: out to one side of the line it falls on (the other side's), best first, by
    // the same rule as those who come up from behind: never close to the player, never before their eyes unless far.
    inline std::vector<Vec> FlankPoints(const Line& enemy, Vec player, float playerFacing) {
        const Vec tries[] = {Along(enemy.centre, enemy.facing + 90.f, 2400.f), Along(enemy.centre, enemy.facing - 90.f, 2400.f),
                             Along(enemy.centre, enemy.facing + 90.f, 3200.f), Along(enemy.centre, enemy.facing - 90.f, 3200.f),
                             Along(Along(enemy.centre, enemy.facing, -1200.f), enemy.facing + 90.f, 2400.f),
                             Along(Along(enemy.centre, enemy.facing, -1200.f), enemy.facing - 90.f, 2400.f)};
        struct Scored {
            Vec at;
            float score;
        };
        std::vector<Scored> scored;
        for (const Vec& at : tries) {
            const float distance = Dist(player, at);
            if (distance < kReserveClear || distance > 7600.f) continue;
            const bool inView = Apart(Bearing(player, at), playerFacing) <= kViewHalfAngle;
            float score = (std::min)(distance, 5000.f);
            if (inView && distance < kNearInView) score -= 3000.f;
            scored.push_back({at, score});
        }
        std::stable_sort(scored.begin(), scored.end(), [](const Scored& l, const Scored& r) { return l.score > r.score; });
        std::vector<Vec> out;
        for (const auto& s : scored) out.push_back(s.at);
        return out;
    }

    // ---- where an army goes when it is over

    // The beaten run, the winners walk off: toward a town, by the roads the game's people travel on. (They were sent to
    // a point fourteen thousand units off in a straight line. On the plain by Whiterun that point can be walked to; by
    // Honningbrew it lies in the mountain, and with no way to it nobody took a step: fifteen soldiers of both armies
    // stood where the fighting had stopped, minutes after it, the beaten among the winners. Played 2026-11-21.)
    // Home if it is far enough to march to; else the nearest town that is. The beaten never run through the army
    // that beat them: for them a town on the far side of the enemy comes last.
    struct Town {
        Vec at;
        std::uint32_t id = 0;       // the town's marker
        bool home = false;          // of the army in question
    };
    inline constexpr float kMarchFar = 16000.f;         // nearer than this is no marching off: they would arrive in view
    // field: where the battle was. away: from the other army toward this one (need not be of length one).
    // Returns the place in the list, -1 when there is none to march to.
    inline int MarchTo(const std::vector<Town>& towns, Vec field, Vec away, bool beaten, std::uint32_t notTo) {
        const float awayLength = std::sqrt(away.x * away.x + away.y * away.y);
        int best = -1;
        float bestScore = 0.f;
        for (std::size_t i = 0; i < towns.size(); ++i) {
            const Town& town = towns[i];
            if (town.id == 0 || town.id == notTo) continue;
            const float distance = Dist(field, town.at);
            if (distance < kMarchFar) continue;
            float score = distance;
            // Through the enemy: more than a hundred degrees off the way that leads away from them.
            bool through = false;
            if (awayLength > 0.f) {
                const float dot = ((town.at.x - field.x) * away.x + (town.at.y - field.y) * away.y) / (distance * awayLength);
                through = dot < -0.1736f;
            }
            if (beaten && through) score += 400000.f;
            else if (town.home) score -= 200000.f;
            if (best < 0 || score < bestScore) {
                best = static_cast<int>(i);
                bestScore = score;
            }
        }
        return best;
    }

    // ---- the player

    inline constexpr int kHostileStanding = -40;    // at or below: that side takes the player for an enemy
    inline constexpr int kFriendStanding = 20;      // at or above: that side counts the player a friend
    // Would this side have the player in its line?
    inline constexpr bool WouldHave(int standing) { return standing > kHostileStanding; }

    // What the winning side pays someone who fought in its line: a soldier's pay, and more for every enemy they cut down.
    inline constexpr int Pay(bool won, int kills) {
        if (!won) return 0;
        const int pay = 120 + 60 * (kills < 0 ? 0 : kills);
        return pay > 600 ? 600 : pay;
    }

    // The player has gone from the field (so far that the armies are no longer in the world around them).
    inline constexpr float kGoneDistance = 9000.f;
}
