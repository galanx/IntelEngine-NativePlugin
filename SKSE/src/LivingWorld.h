#pragma once

// IntelEngine 4.0, the living world: one record per NPC the player knows, and the director that decides
// what reaches the player, when and how. Pure logic without game types, checked offline by
// SKSE/tests/LivingWorldCheck.cpp; the game side feeds facts in and carries the results out.
//
// The rules this file enforces:
//  * Truth comes in this order: what the player can see right now, then the player's running orders and
//    tasks, then the simulated record. Someone who is with the player, on a task, near the player or seen
//    recently is never said to be anywhere else.
//  * Lives move on only while unobserved. Details (memories, rumors, placing the NPC) are made true when
//    the player is about to meet the result, so the cost follows what the player experiences.
//  * Moments land at natural seams (arriving in town, leaving a dungeon, waking up, coming home), paced by
//    a budget, with a rest after a big one. Interruptions at bad moments follow a setting.
//  * Picks rotate: never the same NPC or the same kind twice running, and about one pick in four goes to an
//    acquaintance the player has not seen for a long time.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace IntelEngine::LivingWorld {

    // ------------------------------------------------------------------ settings

    enum class WorldLevel : std::uint8_t { Classic = 0, Rumors = 1, Lives = 2 };
    enum class Interruptions : std::uint8_t { Never = 0, OnlyUrgent = 1, Realistic = 2 };
    enum class QuestNpcMoves : std::uint8_t { Never = 0, ImportantOnly = 1, Freely = 2 };
    enum class Pacing : std::uint8_t { Calm = 0, Normal = 1, Lively = 2 };

    struct Settings {
        WorldLevel level = WorldLevel::Lives;
        Interruptions interruptions = Interruptions::OnlyUrgent;
        QuestNpcMoves questNpcMoves = QuestNpcMoves::ImportantOnly;
        Pacing pacing = Pacing::Normal;
        float waitOrderDays = 30.0f;  // a follower told to wait keeps waiting this long
        float lifeStartDays = 7.0f;   // a follower left behind starts a life of their own after this long
    };

    inline float MomentsPerDay(Pacing pacing) {
        switch (pacing) {
            case Pacing::Calm: return 0.5f;
            case Pacing::Lively: return 2.0f;
            default: return 1.0f;
        }
    }

    // ------------------------------------------------------------------ the record

    enum Flag : std::uint16_t {
        kWithPlayer   = 1 << 0,  // following the player right now
        kOnTask       = 1 << 1,  // carrying out one of the player's orders (a task slot)
        kNearPlayer   = 1 << 2,  // loaded around the player
        kSeenRecently = 1 << 3,  // the player saw them within the last hours
        kInScene      = 1 << 4,  // in a scene (vanilla or another mod's)
        kQuestTarget  = 1 << 5,  // the target of an objective the player has open
        kQuestBound   = 1 << 6,  // in an alias of a running vanilla quest
        kWaitOrder    = 1 << 7,  // the player told them to wait somewhere
        kKeepsShop    = 1 << 8,  // merchant, trainer, steward: keeps working, only short trips at night
        kWasFollower  = 1 << 9,  // has followed the player at some point
        kNeedsRelease = 1 << 10, // the package of their own life must come off (back with the player, a task, a story)
        kGreeted      = 1 << 11, // has greeted the player since the current step of their own life began
    };

    enum class Activity : std::uint8_t { Home = 0, WithPlayer, OnTask, Waiting, Working, Travelling, Adventuring, Visiting };

    struct Life {
        std::uint32_t actor = 0;
        std::uint16_t flags = 0;
        Activity activity = Activity::Home;
        std::uint8_t lastKind = 0;       // kind of the last moment they were part of, 0 = none
        float bond = 0.0f;               // 0..1, from SkyrimNet's engagement data and the relationship mod
        float lastMet = 0.0f;            // game days: the last time the player and they were together
        float lastPicked = -1.0e6f;      // game days: the last moment they were part of
        float parkedSince = -1.0f;       // game days: when the player left them behind, -1 = never
        std::uint32_t parkedAt = 0;      // location where the player left them
        std::uint32_t place = 0;         // where the record says they are, 0 = their usual place
        std::uint32_t companion = 0;     // who they are with, 0 = alone
        float nextChange = 0.0f;         // game days: when their off-screen life may move on
        std::int32_t rumor = 0;          // SkyrimNet world-knowledge entry of what people heard of them, 0 = none
        std::uint16_t steps = 0;         // steps of their own life since the player left them
        float stepAt = -1.0f;            // game days: when the current step began
    };

    constexpr std::size_t kMaxLives = 64;

    inline bool Has(const Life& life, std::uint16_t flag) { return (life.flags & flag) != 0; }

    // Reality pins an NPC: nothing may say they are anywhere else.
    inline bool PinnedByReality(const Life& life) {
        return Has(life, static_cast<std::uint16_t>(kWithPlayer | kOnTask | kNearPlayer | kSeenRecently | kInScene));
    }

    // May the record move this NPC somewhere else while the player is away?
    inline bool MayRelocate(const Life& life, const Settings& settings, bool important) {
        if (settings.level == WorldLevel::Classic) return false;
        if (PinnedByReality(life) || Has(life, kQuestTarget)) return false;
        if (Has(life, kKeepsShop)) return false;  // they keep the shop open; short trips are visits, not moves
        if (Has(life, kQuestBound)) {
            if (settings.questNpcMoves == QuestNpcMoves::Never) return false;
            if (settings.questNpcMoves == QuestNpcMoves::ImportantOnly && !important) return false;
        }
        return true;
    }

    // A follower the player left behind starts a life of their own only at the Lives level and only after a
    // while apart, longer when the player told them to wait.
    inline bool LifeMayBegin(const Life& life, const Settings& settings, float now) {
        if (settings.level != WorldLevel::Lives || !Has(life, kWasFollower) || life.parkedSince < 0.0f) return false;
        if (PinnedByReality(life) || Has(life, kQuestTarget)) return false;
        const float apart = Has(life, kWaitOrder) ? settings.waitOrderDays : settings.lifeStartDays;
        return now - life.parkedSince >= apart;
    }

    // Followers who stopped following and are not merely away on one of the player's tasks (a task clears the
    // follower state while it runs) were left behind by the player.
    inline std::vector<std::uint32_t> LeftBehind(std::span<const std::uint32_t> followersBefore,
                                                 std::span<const std::uint32_t> followersNow,
                                                 std::span<const std::uint32_t> onTask) {
        std::vector<std::uint32_t> left;
        for (const auto actor : followersBefore) {
            if (std::find(followersNow.begin(), followersNow.end(), actor) != followersNow.end()) continue;
            if (std::find(onTask.begin(), onTask.end(), actor) != onTask.end()) continue;
            left.push_back(actor);
        }
        return left;
    }

    // Keep the record small: former followers first, then the closest bonds, then whoever was met most recently.
    inline void Trim(std::vector<Life>& lives, std::size_t max = kMaxLives) {
        if (lives.size() <= max) return;
        std::stable_sort(lives.begin(), lives.end(), [](const Life& a, const Life& b) {
            const bool fa = Has(a, kWasFollower), fb = Has(b, kWasFollower);
            if (fa != fb) return fa;
            if (a.bond != b.bond) return a.bond > b.bond;
            return a.lastMet > b.lastMet;
        });
        lives.resize(max);
    }

    // ------------------------------------------------------------------ lives of followers left behind

    // Living a life of their own: the life's package is on them and the record says where they are.
    inline bool OwnLife(const Life& life) {
        return life.activity == Activity::Working || life.activity == Activity::Adventuring ||
               life.activity == Activity::Travelling || life.activity == Activity::Visiting;
    }

    // A real place a life can go: an inn, a dungeon or a town, with the hold it lies in.
    struct PlaceOption {
        std::uint32_t location = 0;
        std::uint32_t hold = 0;
    };

    struct LifeStep {
        Activity activity = Activity::Home;
        std::uint32_t place = 0;       // 0 = their own home
        std::uint32_t companion = 0;   // another former follower who goes along, 0 = alone
        float until = 0.0f;            // game days: when the next step may come
    };

    namespace detail {
        inline std::uint32_t Mix(std::uint32_t a, std::uint32_t b) {
            std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
            h ^= h >> 15;
            h *= 0x2C1B3C6Du;
            h ^= h >> 12;
            return h;
        }

        // One of the places, those of the given hold first; never the one they are at already.
        inline std::uint32_t PickPlace(std::span<const PlaceOption> options, std::uint32_t hold, std::uint32_t avoid,
                                       std::uint32_t seed) {
            std::vector<std::uint32_t> inHold, anywhere;
            for (const auto& o : options) {
                if (o.location == 0 || o.location == avoid) continue;
                anywhere.push_back(o.location);
                if (hold != 0 && o.hold == hold) inHold.push_back(o.location);
            }
            const auto& pool = inHold.empty() ? anywhere : inHold;
            if (pool.empty()) return 0;
            return pool[seed % pool.size()];
        }
    }

    // What a follower left behind does next: work at an inn of their hold most often, an adventure at a dungeon of
    // the hold (now and then with another former follower who is free), a trip to a town, or home for a while.
    // Every place is one of the given real places; a step lasts days, never hours. "others" are the lives that may
    // go along (the caller has checked they are free and unseen).
    inline LifeStep NextStep(const Life& life, std::uint32_t hold, std::span<const PlaceOption> inns,
                             std::span<const PlaceOption> dungeons, std::span<const PlaceOption> towns,
                             std::span<const Life> others, float now, std::uint32_t seed) {
        const std::uint32_t roll = detail::Mix(seed, life.actor) % 100u;
        LifeStep step;
        auto days = [&](float least, float most) {
            const float span = most - least;
            return now + least + span * static_cast<float>(detail::Mix(seed + 7u, life.actor) % 1000u) / 1000.0f;
        };
        // Back from an adventure or a trip: work or home next, not another adventure at once.
        const bool restless = life.activity == Activity::Adventuring || life.activity == Activity::Travelling;
        if (roll < 45 || (restless && roll < 75)) {
            step.activity = Activity::Working;
            step.place = detail::PickPlace(inns, hold, life.place, detail::Mix(seed, 1u));
            step.until = days(4.0f, 9.0f);
        } else if (roll < 75) {
            step.activity = Activity::Adventuring;
            step.place = detail::PickPlace(dungeons, hold, life.place, detail::Mix(seed, 2u));
            step.until = days(2.0f, 4.0f);
            for (const auto& other : others) {
                const bool free = Has(other, kWasFollower) && other.actor != life.actor && !PinnedByReality(other) &&
                                  !Has(other, kQuestTarget) && !Has(other, kQuestBound) && !Has(other, kKeepsShop) &&
                                  (other.activity == Activity::Home || other.activity == Activity::Working) &&
                                  other.companion == 0 && other.parkedSince >= 0.0f;
                if (free && detail::Mix(seed, other.actor) % 3u == 0u) {
                    step.companion = other.actor;
                    break;
                }
            }
        } else if (roll < 90) {
            step.activity = Activity::Travelling;
            step.place = detail::PickPlace(towns, hold, life.place, detail::Mix(seed, 3u));
            step.until = days(1.0f, 3.0f);
        } else {
            step.activity = Activity::Home;
            step.until = days(5.0f, 10.0f);
        }
        if (step.activity != Activity::Home && step.place == 0) {  // no such place in this load order: home
            step = LifeStep{Activity::Home, 0, 0, days(5.0f, 10.0f)};
        }
        return step;
    }

    // How long ago, in words: "today", "a day ago", "5 days ago", "about 3 weeks ago", "about 2 months ago".
    inline std::string Ago(float days) {
        if (!(days >= 1.0f)) return "today";
        const int whole = static_cast<int>(days);
        if (whole < 2) return "a day ago";
        if (whole < 14) return std::to_string(whole) + " days ago";
        if (whole < 60) return "about " + std::to_string((whole + 3) / 7) + " weeks ago";
        return "about " + std::to_string((whole + 15) / 30) + " months ago";
    }

    // The memory a step leaves with the NPC, first person and past, so it stays true: {player}, {place}, {parked},
    // {companion} are filled in with real names (" at {parked}" goes when the place is not known).
    inline const char* StepMemory(Activity activity, bool firstStep, bool withCompanion) {
        switch (activity) {
            case Activity::Working:
                return firstStep ? "After {player} and I parted ways at {parked}, I made my way to {place} and found work there to earn my keep."
                                 : "I moved on to {place} and found work there to earn my keep.";
            case Activity::Adventuring:
                if (firstStep) {
                    return withCompanion ? "After {player} and I parted ways at {parked}, I set out with {companion} to clear out {place}."
                                         : "After {player} and I parted ways at {parked}, I set out alone to clear out {place}.";
                }
                return withCompanion ? "I set out with {companion} to clear out {place}."
                                     : "I set out alone to clear out {place}.";
            case Activity::Travelling:
                return firstStep ? "After {player} and I parted ways at {parked}, I took to the road and travelled to {place}."
                                 : "I took to the road and travelled to {place}.";
            case Activity::Home:
                return firstStep ? "After {player} and I parted ways at {parked}, I went back home."
                                 : "I went back home for a while.";
            default:
                return "";
        }
    }

    // What people heard (a SkyrimNet world-knowledge entry), past tense: {name} is the former follower.
    inline const char* StepRumor(Activity activity, bool withCompanion) {
        switch (activity) {
            case Activity::Working: return "{name}, who used to travel with {player}, took work at {place}.";
            case Activity::Adventuring:
                return withCompanion ? "{name}, who used to travel with {player}, set out with {companion} to clear out {place}."
                                     : "{name}, who used to travel with {player}, set out alone to clear out {place}.";
            case Activity::Travelling: return "{name}, who used to travel with {player}, was seen on the road to {place}.";
            default: return "";
        }
    }

    // The NPC's own view of where their life stands, for their prompt (rendered each time, so it is never stale):
    // {player}, {parked}, {place}, {companion}, {ago}. Empty when there is nothing of their own to tell.
    inline const char* OwnLifeLine(const Life& life) {
        if (Has(life, kWithPlayer)) {
            return Has(life, kWaitOrder) ? "I am waiting for {player} at {parked}, as {player} asked of me {ago}." : "";
        }
        if (!Has(life, kWasFollower) || life.parkedSince < 0.0f || life.steps == 0) return "";
        switch (life.activity) {
            case Activity::Working:
                return "Since {player} and I parted ways at {parked} {ago}, I have found work at {place} to earn my keep.";
            case Activity::Adventuring:
                return life.companion ? "Since {player} and I parted ways at {parked} {ago}, I have gone with {companion} to clear out {place}."
                                      : "Since {player} and I parted ways at {parked} {ago}, I have gone to clear out {place}.";
            case Activity::Travelling:
                return "Since {player} and I parted ways at {parked} {ago}, I have taken to the road and come to {place}.";
            case Activity::Home:
                return "Since {player} and I parted ways at {parked} {ago}, I have lived my own life, and now I am back home.";
            default:
                return "";
        }
    }

    // The same for the story director, third person.
    inline const char* DirectorLifeLine(const Life& life) {
        if (Has(life, kWithPlayer)) return Has(life, kWaitOrder) ? "waiting at {parked}, as {player} told them {ago}" : "";
        if (!Has(life, kWasFollower) || life.parkedSince < 0.0f || life.steps == 0) return "";
        switch (life.activity) {
            case Activity::Working: return "parted from {player} at {parked} {ago}; has found work at {place}";
            case Activity::Adventuring:
                return life.companion ? "parted from {player} at {parked} {ago}; has gone with {companion} to clear out {place}"
                                      : "parted from {player} at {parked} {ago}; has gone to clear out {place}";
            case Activity::Travelling: return "parted from {player} at {parked} {ago}; on the road, now in {place}";
            case Activity::Home: return "parted from {player} at {parked} {ago}; lived their own life and is back home";
            default: return "";
        }
    }

    // ------------------------------------------------------------------ the director

    enum class Seam : std::uint8_t {
        Timer = 0,      // the old clock, kept as a fallback
        ArrivedInTown,
        EnteredInn,
        LeftDungeon,
        OnRoad,
        Woke,
        CameHome,
        QuestDone,
        InDungeon,
        InCombat,
    };

    enum class Delivery : std::uint8_t {
        None = 0,
        InConversation,  // they are here: it comes up when the player talks to them
        WaitingThere,    // they are here, waiting for the player
        Rumor,           // the innkeeper or townsfolk know about it
        Letter,          // a courier brings it
        ChanceMeeting,   // met on the player's road
        Visit,           // they come to find the player
    };

    // What kind of place the player left or entered (from the location's keywords and its parents').
    struct PlaceKind {
        bool dungeon = false;
        bool inn = false;
        bool town = false;
        bool playerHouse = false;
    };

    // Which seam a change of location is. Entering a dungeon or wandering between wilderness places is not a
    // seam for an ordinary moment (InDungeon / OnRoad); the caller decides whether OnRoad is worth a look.
    inline Seam ClassifySeam(const PlaceKind& from, const PlaceKind& to) {
        if (to.playerHouse && !from.playerHouse) return Seam::CameHome;
        if (to.inn && !from.inn) return Seam::EnteredInn;
        if (from.dungeon && !to.dungeon) return Seam::LeftDungeon;
        if (to.dungeon) return Seam::InDungeon;
        if (to.town && !from.town) return Seam::ArrivedInTown;
        return Seam::OnRoad;
    }

    struct DirectorState {
        float budget = 1.0f;             // moments available; refilled by the pacing, at most 2
        float budgetDay = 0.0f;          // game day of the last refill
        float lastMomentDay = -1.0e6f;
        float lastBigDay = -1.0e6f;
        std::uint32_t picks = 0;         // moments delivered, for the acquaintance rotation
        std::uint32_t lastActor = 0;
        std::uint8_t lastKind = 0;
    };

    struct Situation {
        Seam seam = Seam::Timer;
        float now = 0.0f;                // game days
        bool badMoment = false;          // a scene, sleep, combat, a menu or a conversation is going on
        float playedSince = -1.0f;       // seconds of play since the last moment; below 0: none this session
    };

    struct Candidate {
        const Life* life = nullptr;
        std::uint8_t kind = 0;           // the story kind this moment would be
        bool unresolved = false;         // open business between them and the player
        bool urgent = false;             // real danger or need: may interrupt
        bool big = false;                // a big moment earns the player a rest afterwards
        bool here = false;               // the record has them where the player is now
        bool questLink = false;          // tied to a place of the vanilla quest the player is on
        float onRoute = 0.0f;            // 0..1: on or near the player's way
        float tripDays = 0.0f;           // how long reaching the player would take them
    };

    constexpr float kMaxBudget = 2.0f;
    constexpr float kMinGapDays = 0.125f;     // three game hours between ordinary moments
    // And time actually played between them. Everything above counts the game's clock, which the player can turn
    // forward at will: whoever waits a day, and then another, was owed a moment after each wait (the user,
    // 2026-11-22: "What about player constantly skipping time. Will that ruin the pacing?"). Played time is the time
    // the game ran, not paused and not skipped. Eight minutes is what three game hours are at the game's own pace
    // (a timescale of 20), so in ordinary play this never binds; it binds when time is skipped or runs fast.
    constexpr float kMinGapPlayedSeconds = 480.0f;
    constexpr float kRestAfterBigDays = 1.0f;
    constexpr float kForgottenBond = 0.35f;
    constexpr float kForgottenDays = 20.0f;
    constexpr std::uint32_t kRotationEvery = 4;

    inline void Refill(DirectorState& state, const Settings& settings, float now) {
        if (now > state.budgetDay) {
            state.budget = std::min(kMaxBudget, state.budget + (now - state.budgetDay) * MomentsPerDay(settings.pacing));
        }
        state.budgetDay = now;
    }

    // How welcome a moment is at this seam (0 = only if urgent).
    inline float Openness(Seam seam) {
        switch (seam) {
            case Seam::ArrivedInTown:
            case Seam::EnteredInn:
            case Seam::CameHome: return 1.0f;
            case Seam::LeftDungeon:
            case Seam::Woke: return 0.8f;
            case Seam::QuestDone: return 0.7f;
            case Seam::OnRoad: return 0.6f;
            case Seam::Timer: return 0.4f;
            default: return 0.0f;  // deep in a dungeon or in combat
        }
    }

    inline bool MayDeliverNow(const DirectorState& state, const Settings& settings, const Situation& at,
                              bool urgent, bool big, std::uint32_t seed) {
        if (at.badMoment) {
            if (settings.interruptions == Interruptions::Never) return false;
            if (settings.interruptions == Interruptions::OnlyUrgent && !urgent) return false;
            if (settings.interruptions == Interruptions::Realistic && !urgent && (seed % 4u) != 0u) return false;
        }
        if (Openness(at.seam) <= 0.0f && (!urgent || settings.interruptions == Interruptions::Never)) return false;
        if (urgent) return true;
        if (state.budget < 1.0f) return false;
        if (at.now - state.lastMomentDay < kMinGapDays) return false;
        if (at.playedSince >= 0.0f && at.playedSince < kMinGapPlayedSeconds) return false;
        if (big && at.now - state.lastBigDay < kRestAfterBigDays) return false;
        return true;
    }

    inline bool IsForgottenAcquaintance(const Life& life, float now) {
        return life.bond < kForgottenBond && now - life.lastMet >= kForgottenDays;
    }

    // Small, repeatable variety in [-1, 1) so equal scores do not always resolve the same way.
    inline float Jitter(std::uint32_t seed, std::uint32_t actor) {
        std::uint32_t h = seed * 2654435761u ^ (actor + 0x9E3779B9u + (seed << 6) + (seed >> 2));
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        return static_cast<float>(h % 2000u) / 1000.0f - 1.0f;
    }

    // Who may be part of a moment at all. Someone with the player or on a task never gets an elsewhere-moment
    // (their story goes on through the task itself); someone near the player only gets a here-moment.
    inline bool Eligible(const Candidate& c) {
        if (!c.life) return false;
        const Life& life = *c.life;
        if (Has(life, kWithPlayer) || Has(life, kOnTask) || Has(life, kInScene)) return false;
        if ((Has(life, kNearPlayer) || Has(life, kSeenRecently)) && !c.here) return false;
        return true;
    }

    inline float Score(const Candidate& c, const DirectorState& state, const Situation& at, std::uint32_t seed) {
        const Life& life = *c.life;
        const float absence = std::max(0.0f, at.now - life.lastMet);
        float score = life.bond * std::min(absence / 7.0f, 3.0f) * 1.5f;  // the closer they are, the sooner they miss you
        if (IsForgottenAcquaintance(life, at.now)) score += 0.6f + std::min((absence - kForgottenDays) / 30.0f, 1.0f);
        if (c.unresolved) score += 1.5f;
        if (c.urgent) score += 2.0f;
        if (c.here) score += 1.0f;
        if (c.questLink) score += 0.8f;
        score += std::clamp(c.onRoute, 0.0f, 1.0f);
        if (life.actor == state.lastActor) score -= 3.0f;
        if (c.kind != 0 && c.kind == state.lastKind) score -= 1.0f;
        const float sincePicked = at.now - life.lastPicked;
        if (sincePicked < 3.0f) score -= 2.0f * (1.0f - std::max(0.0f, sincePicked) / 3.0f);
        if (c.tripDays > 1.0f && !c.unresolved && !c.urgent) score -= std::min(c.tripDays - 1.0f, 3.0f) * 0.5f;
        score += Jitter(seed, life.actor) * 0.3f;
        return score;
    }

    // The best candidate, or nothing. Every kRotationEvery-th moment goes to a forgotten acquaintance when one
    // is eligible, so people the player met once and lost track of come back now and then.
    inline std::optional<std::size_t> Pick(std::span<const Candidate> candidates, const DirectorState& state,
                                            const Situation& at, std::uint32_t seed) {
        const bool rotation = state.picks % kRotationEvery == kRotationEvery - 1;
        bool anyForgotten = false;
        if (rotation) {
            for (const auto& c : candidates) {
                if (Eligible(c) && IsForgottenAcquaintance(*c.life, at.now)) anyForgotten = true;
            }
        }
        std::optional<std::size_t> best;
        float bestScore = 0.0f;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto& c = candidates[i];
            if (!Eligible(c)) continue;
            if (anyForgotten && !IsForgottenAcquaintance(*c.life, at.now)) continue;
            const float score = Score(c, state, at, seed);
            if (!best || score > bestScore) {
                best = i;
                bestScore = score;
            }
        }
        if (best && bestScore <= 0.0f && !candidates[*best].urgent && !anyForgotten) return std::nullopt;  // nobody worth it
        return best;
    }

    // How the moment reaches the player, from where the player is and how far away the NPC is.
    inline Delivery ChooseDelivery(const Candidate& c, const Situation& at) {
        const Life& life = *c.life;
        if (c.here) {
            const bool waits = at.seam == Seam::ArrivedInTown || at.seam == Seam::EnteredInn || at.seam == Seam::CameHome;
            return waits ? Delivery::WaitingThere : Delivery::InConversation;
        }
        const float absence = at.now - life.lastMet;
        const bool worthTheTrip = c.urgent || (life.bond >= 0.6f && absence >= 14.0f) || c.unresolved;
        switch (at.seam) {
            case Seam::ArrivedInTown:
            case Seam::EnteredInn:
                return Delivery::Rumor;
            case Seam::OnRoad:
            case Seam::LeftDungeon:
                if (c.onRoute >= 0.6f) return Delivery::ChanceMeeting;
                return (worthTheTrip && c.tripDays <= 0.5f) ? Delivery::Visit : Delivery::Letter;
            case Seam::Woke:
            case Seam::CameHome:
                return (worthTheTrip && c.tripDays <= 2.0f) ? Delivery::Visit : Delivery::Letter;
            case Seam::InDungeon:
            case Seam::InCombat:
                return (c.urgent && c.tripDays <= 0.25f) ? Delivery::Visit : Delivery::None;
            default:
                return (c.urgent && c.tripDays <= 1.0f) ? Delivery::Visit : Delivery::Letter;
        }
    }

    inline void Commit(DirectorState& state, Life& life, const Candidate& c, const Situation& at) {
        if (!c.urgent) state.budget = std::max(0.0f, state.budget - 1.0f);
        state.lastMomentDay = at.now;
        if (c.big) state.lastBigDay = at.now;
        ++state.picks;
        state.lastActor = life.actor;
        state.lastKind = c.kind;
        life.lastPicked = at.now;
        life.lastKind = c.kind;
    }

    // ------------------------------------------------------------------ save data (co-save record 'IELW')

    constexpr std::uint32_t kRecordType = 'IELW';
    constexpr std::uint32_t kRecordVersion = 2;

    namespace detail {
        template <class T>
        void Put(std::vector<std::uint8_t>& out, const T& value) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
            out.insert(out.end(), p, p + sizeof(T));
        }
        template <class T>
        bool Get(std::span<const std::uint8_t> in, std::size_t& at, T& value) {
            if (at > in.size() || in.size() - at < sizeof(T)) return false;
            std::memcpy(&value, in.data() + at, sizeof(T));
            at += sizeof(T);
            return true;
        }
    }

    inline std::vector<std::uint8_t> Serialize(std::span<const Life> lives, const DirectorState& state) {
        std::vector<std::uint8_t> out;
        const auto count = static_cast<std::uint32_t>(std::min(lives.size(), kMaxLives));
        detail::Put(out, count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const Life& l = lives[i];
            detail::Put(out, l.actor);
            detail::Put(out, l.flags);
            detail::Put(out, static_cast<std::uint8_t>(l.activity));
            detail::Put(out, l.lastKind);
            detail::Put(out, l.bond);
            detail::Put(out, l.lastMet);
            detail::Put(out, l.lastPicked);
            detail::Put(out, l.parkedSince);
            detail::Put(out, l.parkedAt);
            detail::Put(out, l.place);
            detail::Put(out, l.companion);
            detail::Put(out, l.nextChange);
            detail::Put(out, l.rumor);
            detail::Put(out, l.steps);
            detail::Put(out, l.stepAt);
        }
        detail::Put(out, state.budget);
        detail::Put(out, state.budgetDay);
        detail::Put(out, state.lastMomentDay);
        detail::Put(out, state.lastBigDay);
        detail::Put(out, state.picks);
        detail::Put(out, state.lastActor);
        detail::Put(out, state.lastKind);
        return out;
    }

    // All or nothing: on short or damaged data the outputs are left as they were and false comes back.
    inline bool Deserialize(std::span<const std::uint8_t> in, std::vector<Life>& lives, DirectorState& state) {
        std::size_t at = 0;
        std::uint32_t count = 0;
        if (!detail::Get(in, at, count) || count > kMaxLives) return false;
        std::vector<Life> read(count);
        for (auto& l : read) {
            std::uint8_t activity = 0;
            if (!detail::Get(in, at, l.actor) || !detail::Get(in, at, l.flags) || !detail::Get(in, at, activity) ||
                !detail::Get(in, at, l.lastKind) || !detail::Get(in, at, l.bond) || !detail::Get(in, at, l.lastMet) ||
                !detail::Get(in, at, l.lastPicked) || !detail::Get(in, at, l.parkedSince) ||
                !detail::Get(in, at, l.parkedAt) || !detail::Get(in, at, l.place) ||
                !detail::Get(in, at, l.companion) || !detail::Get(in, at, l.nextChange) ||
                !detail::Get(in, at, l.rumor) || !detail::Get(in, at, l.steps) || !detail::Get(in, at, l.stepAt)) {
                return false;
            }
            if (activity > static_cast<std::uint8_t>(Activity::Visiting) || !std::isfinite(l.bond)) return false;
            l.activity = static_cast<Activity>(activity);
        }
        DirectorState d;
        if (!detail::Get(in, at, d.budget) || !detail::Get(in, at, d.budgetDay) || !detail::Get(in, at, d.lastMomentDay) ||
            !detail::Get(in, at, d.lastBigDay) || !detail::Get(in, at, d.picks) || !detail::Get(in, at, d.lastActor) ||
            !detail::Get(in, at, d.lastKind) || at != in.size()) {
            return false;
        }
        lives = std::move(read);
        state = d;
        return true;
    }
}
