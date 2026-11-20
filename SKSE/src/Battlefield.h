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
