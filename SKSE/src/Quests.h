#pragma once

// IntelEngine 4.0 quests (docs/QUESTS.md): multi-step quests whose every step is a fact of the game, never a word
// of the dialogue. Pure logic without game types, checked offline by SKSE/tests/QuestsCheck.cpp; QuestBridge reads
// the facts from the game and the scripts (IntelEngine_Quests.psc) show the journal and place what the quest needs.
//
// A quest is a template (a short graph of steps), a cast of real forms chosen by the engine, and a truth rolled when
// it is made and never changed. The dialogue model writes only words, from what the record says; nothing anyone
// says moves a step, so a quest runs the same in any language and on any model.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IntelEngine::Quests {

    enum class Template : std::uint8_t {
        None = 0,
        Keepsake = 1,  // something dear to the giver was stolen: search the place, recover it, bring it back
        Bounty = 2,    // a named foe and their gang settled near the giver: deal with them, tell the giver
        Venture = 3,   // a former follower on an adventure of their own: go with them, clear the place, share
    };

    // What really happened, fixed when the quest is made.
    enum class Truth : std::uint8_t {
        None = 0,
        ThiefWaits = 1,   // keepsake: the thief and a few of their kind wait at the place with it
        ThiefDead = 2,    // keepsake: the thief lies dead there, killed by what lives in the place, the item on them
        FoePlain = 3,     // bounty: the foe and their gang
        FoeSpoils = 4,    // bounty: the foe carries something stamped of another person of the hold
        PlaceHeld = 5,    // venture: the place still has its own master (cleared when the location is)
        PlaceRetaken = 6, // venture: the place was cleared before and something moved in (a foe is placed)
    };

    enum class Phase : std::uint8_t {
        Planned = 0,  // decided; the giver is on the way or the meeting is set up. Not in the journal yet.
        Open = 1,     // taken on: in the journal
        Ended = 2,
    };

    enum class Ending : std::uint8_t {
        None = 0,
        Done = 10,       // what was asked is done
        Kept = 11,       // keepsake: the player found it and never brought it back
        Declined = 12,   // the player said no when it was offered
        Ignored = 13,    // the time ran out and nothing was done
        GiverDied = 14,  // the one who asked is dead
        WentAlone = 15,  // venture: the player never came along, so they went on their own
        AllyDied = 16,   // venture: the one they went with died
        Lapsed = 17,     // never offered (the giver never reached the player): nobody remembers anything
    };

    enum class StepKind : std::uint8_t {
        Reach = 1,          // the player is at the place
        Recover = 2,        // the stamped item is in the player's inventory
        HandOver = 3,       // the player gave the stamped item to the giver (the engine moved it)
        Defeat = 4,         // the foe is dead, by anyone's hand
        Talk = 5,           // the player is with someone while they speak
        Clear = 6,          // the place's master is dead (the foe placed there, or the location's own boss)
        ReachTogether = 7,  // the player and the ally are both at the place
    };

    // Who a step is about, and where its marker points.
    enum class Role : std::uint8_t { None = 0, Giver, Foe, Ally, Holder, Anchor };

    // The objective phrasings of a journal quest (esp/EspBuilder QuestPhrasings, same order).
    namespace Objective {
        constexpr std::uint8_t Search = 0;   // Search <Place> for <Item>
        constexpr std::uint8_t Recover = 1;  // Recover <Item>
        constexpr std::uint8_t Bring = 2;    // Bring <Item> to <Giver>
        constexpr std::uint8_t DealWith = 3; // Deal with <Foe> at <Place>
        constexpr std::uint8_t Report = 4;   // Tell <Giver> it is done
        constexpr std::uint8_t GoWith = 5;   // Go with <Ally> to <Place>
        constexpr std::uint8_t Clear = 6;    // Clear <Place> with <Ally>
        constexpr std::uint8_t Share = 7;    // Share the spoils with <Ally>
        constexpr std::uint8_t Count = 8;
    }

    struct StepDef {
        StepKind kind;
        std::uint8_t objective;
        Role marker;
        Role who;              // whose presence or words the step needs (Talk), else None
        std::uint16_t logStage;  // the journal stage set when this step is done (its log entry), 0 = none
        Ending ends;           // None = the quest goes on
        float extendDays;      // added to the deadline when the step is done (a venture has no hurry once begun)
    };

    // The journal stages: <template> * 100 opens it, + 10.. the steps, + 90 + n the endings.
    constexpr std::uint16_t OpenStage(Template t) { return static_cast<std::uint16_t>(static_cast<int>(t) * 100); }
    constexpr std::uint16_t EndStage(Template t, Ending e) {
        int n = 0;
        switch (e) {
            case Ending::Done: n = 0; break;
            case Ending::Kept: n = 1; break;
            case Ending::Ignored: n = 2; break;
            case Ending::GiverDied: n = 3; break;
            case Ending::WentAlone: n = 4; break;
            case Ending::AllyDied: n = 5; break;
            default: return 0;  // declined and lapsed quests never reached the journal
        }
        return static_cast<std::uint16_t>(static_cast<int>(t) * 100 + 90 + n);
    }

    inline std::span<const StepDef> StepsOf(Template t) {
        static constexpr StepDef keepsake[] = {
            {StepKind::Reach, Objective::Search, Role::Anchor, Role::None, 110, Ending::None, 0.0f},
            {StepKind::Recover, Objective::Recover, Role::Holder, Role::None, 120, Ending::None, 0.0f},
            {StepKind::HandOver, Objective::Bring, Role::Giver, Role::Giver, 0, Ending::Done, 0.0f},
        };
        static constexpr StepDef bounty[] = {
            {StepKind::Defeat, Objective::DealWith, Role::Foe, Role::Foe, 210, Ending::None, 1.0f},
            {StepKind::Talk, Objective::Report, Role::Giver, Role::Giver, 0, Ending::Done, 0.0f},
        };
        static constexpr StepDef venture[] = {
            // The marker points at the place, not at the ally: they follow the player there, and a marker over a
            // companion's head walking beside you points nowhere (the user, 2026-11-18).
            {StepKind::ReachTogether, Objective::GoWith, Role::Anchor, Role::Ally, 310, Ending::None, 2.0f},
            {StepKind::Clear, Objective::Clear, Role::Anchor, Role::None, 320, Ending::None, 1.0f},
            {StepKind::Talk, Objective::Share, Role::Ally, Role::Ally, 0, Ending::Done, 0.0f},
        };
        switch (t) {
            case Template::Keepsake: return keepsake;
            case Template::Bounty: return bounty;
            case Template::Venture: return venture;
            default: return {};
        }
    }

    // How long a quest stands once taken on (game days), before the time runs out.
    inline float DaysFor(Template t) {
        switch (t) {
            case Template::Keepsake: return 7.0f;
            case Template::Bounty: return 7.0f;
            case Template::Venture: return 1.0f;  // they will not wait for long; more once the player is with them
            default: return 3.0f;
        }
    }

    // How long a planned quest waits for its offer before it lapses (the giver never reached the player).
    constexpr float kOfferDays = 1.0f;

    // A step may be found done ahead of the steps before it only when its fact implies theirs: the item in hand means
    // the place was searched, the foe dead means they were dealt with. Talking to someone does not: telling the
    // giver "it is done" before the foe is dead is not done.
    inline bool MayJumpTo(StepKind k) {
        return k == StepKind::Reach || k == StepKind::Recover || k == StepKind::HandOver || k == StepKind::Defeat ||
               k == StepKind::Clear;
    }

    // What the game says right now (QuestBridge reads it).
    struct Facts {
        bool inPlace = false;       // the player is at the place (in its location, or near its anchor)
        bool allyInPlace = false;   // the ally is there too
        bool hasItem = false;       // the stamped item is in the player's inventory
        bool handedOver = false;    // the stamped item went to the giver
        bool foeDead = false;
        bool cleared = false;       // the place's master is dead
        bool withGiver = false;
        bool giverSpoke = false;    // the giver spoke a moment ago (SkyrimNet's speech events)
        bool withAlly = false;
        bool allySpoke = false;
        bool giverDead = false;
        bool allyDead = false;
    };

    inline bool StepDone(const StepDef& d, const Facts& f) {
        switch (d.kind) {
            case StepKind::Reach: return f.inPlace;
            case StepKind::Recover: return f.hasItem || f.handedOver;
            case StepKind::HandOver: return f.handedOver;
            case StepKind::Defeat: return f.foeDead;
            case StepKind::Talk:
                return d.who == Role::Giver ? (f.withGiver && f.giverSpoke) : d.who == Role::Ally ? (f.withAlly && f.allySpoke) : false;
            case StepKind::Clear: return f.cleared;
            case StepKind::ReachTogether: return f.inPlace && f.allyInPlace;
        }
        return false;
    }

    constexpr std::size_t kMaxNote = 200;

    struct Quest {
        std::uint32_t id = 0;
        Template tmpl = Template::None;
        Truth truth = Truth::None;
        Phase phase = Phase::Planned;
        Ending ending = Ending::None;
        std::uint8_t slot = 0xFF;      // journal quest 0..kSlots-1, 0xFF = none
        std::uint8_t step = 0;         // the step the player is on
        std::uint8_t doneMask = 0;     // steps done (bit per step)
        std::uint8_t placed = 0;       // Placed:: bits, what the truth needs at the place
        bool handed = false;           // the stamped item went to the giver (the engine moved it)
        std::int32_t rumor = 0;        // the SkyrimNet world-knowledge entry of the open quest, 0 = none
        std::uint32_t giver = 0;       // who asks (keepsake, bounty) / the former follower (venture)
        std::uint32_t foe = 0;         // the thief or the foe (a spawned actor), 0 until placed
        std::uint32_t ally = 0;        // venture: the former follower (same as giver)
        std::uint32_t companion = 0;   // venture: another former follower going along, 0 = none
        std::uint32_t place = 0;       // location
        std::uint32_t anchor = 0;      // the place's boss room marker
        std::uint32_t item = 0;        // the base item stamped (keepsake), or carried by the foe (bounty spoils)
        std::uint32_t third = 0;       // bounty spoils: the person the foe robbed
        std::uint32_t holder = 0;      // who holds the item at the place (the thief, alive or dead)
        std::uint32_t gold = 0;        // the reward, fixed when it is offered
        std::uint32_t seed = 0;
        float created = 0.0f;          // game days
        float opened = 0.0f;
        float since = 0.0f;            // when the last step or phase changed
        float deadline = 0.0f;
        std::string stamp;             // the stamped item's name ("Hulda's Silver Necklace")
        std::string foeName;           // the foe's name, given when they are placed
        std::string title;             // the journal title
    };

    // What has been placed at the quest's place (Quest::placed).
    namespace Placed {
        constexpr std::uint8_t Foe = 1;        // the foe or thief, named, with the stamped item if any
        constexpr std::uint8_t Gang = 2;       // the gang, once the place's cell was loaded
        constexpr std::uint8_t ThiefKilled = 4;  // the thief lies dead (the truth ThiefDead), done once the cell loaded
        constexpr std::uint8_t Item = 8;         // the stamped item is on the thief, done once the cell loaded
        constexpr std::uint8_t Met = 16;         // venture: the player and they have met (they may ask them along now)
        constexpr std::uint8_t Invited = 32;     // venture: they asked the player along (the InviteAlong action)
        constexpr std::uint8_t AtPlace = 64;     // venture: met at the place itself, not on the road
    }

    inline bool IsOpen(const Quest& q) { return q.phase == Phase::Open; }
    inline bool IsLive(const Quest& q) { return q.phase != Phase::Ended; }

    // A step change the scripts show: steps done (indices) and how it ended.
    struct Change {
        std::vector<std::uint8_t> done;
        Ending ending = Ending::None;
        bool any() const { return !done.empty() || ending != Ending::None; }
    };

    // Moves an open quest on from the facts. Failures come first (a dead giver ends it before anything else counts);
    // then the furthest step whose fact holds (only steps that may jump ahead are looked at past the current one);
    // then the deadline.
    inline Change Advance(Quest& q, const Facts& f, float now) {
        Change c;
        if (q.phase != Phase::Open) return c;
        const auto steps = StepsOf(q.tmpl);
        if (steps.empty() || q.step >= steps.size()) return c;
        auto end = [&](Ending e) {
            q.phase = Phase::Ended;
            q.ending = e;
            q.since = now;
            c.ending = e;
        };
        if (q.tmpl == Template::Venture) {
            if (f.allyDead) {
                end(Ending::AllyDied);
                return c;
            }
        } else if (f.giverDead && !f.handedOver) {
            end(Ending::GiverDied);
            return c;
        }
        int reached = -1;
        for (int i = static_cast<int>(steps.size()) - 1; i >= static_cast<int>(q.step); --i) {
            const auto& d = steps[static_cast<std::size_t>(i)];
            if (i != q.step && !MayJumpTo(d.kind)) continue;
            if (StepDone(d, f)) {
                reached = i;
                break;
            }
        }
        if (reached >= 0) {
            for (int i = q.step; i <= reached; ++i) {
                q.doneMask = static_cast<std::uint8_t>(q.doneMask | (1u << i));
                c.done.push_back(static_cast<std::uint8_t>(i));
                q.deadline += steps[static_cast<std::size_t>(i)].extendDays;
            }
            q.step = static_cast<std::uint8_t>(reached + 1);
            q.since = now;
            const Ending e = steps[static_cast<std::size_t>(reached)].ends;
            if (e != Ending::None) {
                end(e);
                return c;
            }
        }
        if (q.deadline > 0.0f && now >= q.deadline) {
            if (q.tmpl == Template::Keepsake && (f.hasItem || (q.doneMask & 0b10))) end(Ending::Kept);
            else if (q.tmpl == Template::Venture && !(q.doneMask & 1)) end(Ending::WentAlone);
            else if (q.tmpl == Template::Venture) {
                // Together at the place and still at it when the time ran out: they finish it with or without the
                // player; nobody is to blame. The deadline moves on rather than ending a quest the two are in.
                q.deadline = now + 1.0f;
            } else end(Ending::Ignored);
        }
        return c;
    }

    // ------------------------------------------------------------------ the book

    constexpr std::size_t kSlots = 6;      // journal quests IntelEngine_Quest0..5
    constexpr std::size_t kMaxOpen = 3;    // at most three at once: a few real quests, not a list of chores
    constexpr std::size_t kMaxKept = 24;   // ended quests are kept a while (memory, follow-ups, completed titles)

    class Book {
    public:
        std::vector<Quest> all;
        std::uint32_t nextId = 1;
        Template lastTemplate = Template::None;
        std::uint8_t lastSlot = 0xFF;

        std::size_t Count(Phase p) const {
            return static_cast<std::size_t>(std::count_if(all.begin(), all.end(), [p](const Quest& q) { return q.phase == p; }));
        }
        Quest* Find(std::uint32_t id) {
            for (auto& q : all) if (q.id == id) return &q;
            return nullptr;
        }
        const Quest* Find(std::uint32_t id) const {
            for (const auto& q : all) if (q.id == id) return &q;
            return nullptr;
        }
        // The live quest (planned or open) this person gives or is the ally of, nullptr = none.
        const Quest* LiveFor(std::uint32_t actor) const {
            for (const auto& q : all) {
                if (IsLive(q) && actor && (q.giver == actor || q.ally == actor || q.companion == actor)) return &q;
            }
            return nullptr;
        }
        bool PlaceInUse(std::uint32_t place) const {
            return std::any_of(all.begin(), all.end(), [place](const Quest& q) { return IsLive(q) && place && q.place == place; });
        }

        // Why a quest may not be planned now (empty = it may).
        std::string Refusal(std::uint32_t giver) const {
            if (!giver) return "nobody to ask";
            if (Count(Phase::Planned) > 0) return "another quest is on its way to the player";
            if (Count(Phase::Open) >= kMaxOpen) return "three quests are open already";
            if (LiveFor(giver)) return "this person has a quest going already";
            return {};
        }

        // Records a planned quest; returns its id (0 = refused).
        std::uint32_t Plan(Quest q, float now) {
            if (!Refusal(q.giver).empty() || q.tmpl == Template::None) return 0;
            q.id = nextId++;
            q.phase = Phase::Planned;
            q.ending = Ending::None;
            q.step = 0;
            q.doneMask = 0;
            q.slot = 0xFF;
            q.created = now;
            q.since = now;
            q.deadline = now + kOfferDays;
            lastTemplate = q.tmpl;
            all.push_back(std::move(q));
            Prune();
            return all.back().id;
        }

        // The player took it on: a journal slot (the least recently used free one, so a completed quest stays in
        // Completed Quests as long as possible), the deadline from now. Returns false when no slot is free.
        bool Accept(std::uint32_t id, float now) {
            Quest* q = Find(id);
            if (!q || q->phase != Phase::Planned) return false;
            std::uint8_t pick = 0xFF;
            for (std::size_t n = 1; n <= kSlots; ++n) {
                const auto slot = static_cast<std::uint8_t>((static_cast<std::size_t>(lastSlot == 0xFF ? kSlots - 1 : lastSlot) + n) % kSlots);
                const bool used = std::any_of(all.begin(), all.end(), [slot](const Quest& o) { return IsOpen(o) && o.slot == slot; });
                if (!used) {
                    pick = slot;
                    break;
                }
            }
            if (pick == 0xFF) return false;
            // A slot is reused: an ended quest that showed there no longer does.
            for (auto& o : all) {
                if (o.id != id && o.slot == pick && !IsOpen(o)) o.slot = 0xFF;
            }
            q->slot = pick;
            lastSlot = pick;
            q->phase = Phase::Open;
            q->opened = now;
            q->since = now;
            q->deadline = now + DaysFor(q->tmpl);
            return true;
        }

        bool Decline(std::uint32_t id, float now) { return EndPlanned(id, Ending::Declined, now); }

        // A planned quest ends before it was ever taken on: declined, or (a venture) the player went another way.
        bool EndPlanned(std::uint32_t id, Ending ending, float now) {
            Quest* q = Find(id);
            if (!q || q->phase != Phase::Planned) return false;
            q->phase = Phase::Ended;
            q->ending = ending;
            q->since = now;
            return true;
        }

        // Planned quests whose offer never came: they lapse, and nobody remembers anything.
        std::vector<std::uint32_t> Lapse(float now) {
            std::vector<std::uint32_t> ids;
            for (auto& q : all) {
                if (q.phase == Phase::Planned && now >= q.deadline) {
                    q.phase = Phase::Ended;
                    q.ending = Ending::Lapsed;
                    q.since = now;
                    ids.push_back(q.id);
                }
            }
            return ids;
        }

        std::vector<std::uint32_t> OpenIds() const {
            std::vector<std::uint32_t> ids;
            for (const auto& q : all) if (IsOpen(q)) ids.push_back(q.id);
            return ids;
        }

    private:
        void Prune() {
            if (all.size() <= kMaxKept) return;
            std::stable_sort(all.begin(), all.end(), [](const Quest& a, const Quest& b) {
                if (IsLive(a) != IsLive(b)) return IsLive(a);
                return a.since > b.since;
            });
            all.resize(kMaxKept);
        }
    };

    // ------------------------------------------------------------------ choices made when a quest is planned

    namespace detail {
        inline std::uint32_t Mix(std::uint32_t a, std::uint32_t b) {
            std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
            h ^= h >> 15;
            h *= 0x2C1B3C6Du;
            h ^= h >> 12;
            return h;
        }
    }

    // A place a quest can use: a dungeon with a boss room in IntelEngine's index.
    struct PlaceOption {
        std::uint32_t location = 0;
        std::uint32_t anchor = 0;
        bool inGiverHold = false;
        bool cleared = false;      // the player cleared it before
        bool crypt = false;        // the dead walk there (draugr)
        float distance = 0.0f;     // from the giver, in game units (0 = unknown)
    };

    // The place for a quest: in the giver's hold when possible, not in use by another quest, not the one the player
    // is in, near enough to walk to (closest third first), varied by the seed.
    inline const PlaceOption* PickPlace(std::span<const PlaceOption> options, const Book& book, std::uint32_t playerLocation,
                                        std::uint32_t seed, bool wantCleared = false) {
        std::vector<const PlaceOption*> pool;
        for (const auto& o : options) {
            if (!o.location || !o.anchor || o.location == playerLocation || book.PlaceInUse(o.location)) continue;
            if (wantCleared && !o.cleared) continue;
            pool.push_back(&o);
        }
        if (pool.empty()) return nullptr;
        const bool anyInHold = std::any_of(pool.begin(), pool.end(), [](const PlaceOption* o) { return o->inGiverHold; });
        if (anyInHold) std::erase_if(pool, [](const PlaceOption* o) { return !o->inGiverHold; });
        std::stable_sort(pool.begin(), pool.end(), [](const PlaceOption* a, const PlaceOption* b) {
            const float da = a->distance > 0.0f ? a->distance : 1e9f;
            const float db = b->distance > 0.0f ? b->distance : 1e9f;
            return da < db;
        });
        // (not "near": a Windows macro)
        const std::size_t closest = std::max<std::size_t>(1, (pool.size() + 2) / 3);
        return pool[detail::Mix(seed, 11u) % closest];
    }

    // The truth of a quest, rolled once.
    inline Truth PickTruth(Template t, const PlaceOption& place, bool hasThird, std::uint32_t seed) {
        const std::uint32_t roll = detail::Mix(seed, 23u) % 100u;
        switch (t) {
            case Template::Keepsake: return roll < 30 ? Truth::ThiefDead : Truth::ThiefWaits;
            case Template::Bounty: return hasThird && roll < 40 ? Truth::FoeSpoils : Truth::FoePlain;
            case Template::Venture: return place.cleared ? Truth::PlaceRetaken : Truth::PlaceHeld;
            default: return Truth::None;
        }
    }

    // Which kind of quest this giver gets: never the one made last when another fits.
    inline Template PickTemplate(bool keepsakeFits, bool bountyFits, Template last, std::uint32_t seed) {
        std::vector<Template> fits;
        if (keepsakeFits) fits.push_back(Template::Keepsake);
        if (bountyFits) fits.push_back(Template::Bounty);
        if (fits.empty()) return Template::None;
        if (fits.size() > 1) std::erase(fits, last);
        return fits[detail::Mix(seed, 31u) % fits.size()];
    }

    // Whether the foe is a woman, by the seed (the name is chosen with it; the spawn is matched to it).
    inline bool FoeFemale(std::uint32_t seed) { return detail::Mix(seed, 47u) % 2u == 1u; }

    // The foe's name: a first name and a by-name, by the seed.
    inline std::string FoeName(std::uint32_t seed, bool female) {
        static constexpr std::string_view men[] = {"Grimvar", "Orlek", "Hroki", "Vigmund", "Borkul", "Ulfgar", "Torsten",
                                                   "Kjell", "Asbjorn", "Haldor", "Rorlund", "Egil", "Sten", "Gunnolf"};
        static constexpr std::string_view women[] = {"Sigrun", "Hrefna", "Valdis", "Torunn", "Ragna", "Ylva", "Asta",
                                                     "Brynja", "Gudrun", "Svala", "Hildr", "Eydis", "Ingrid", "Kolfinna"};
        static constexpr std::string_view bynames[] = {"Half-Hand", "the Crow", "Bone-Eye", "Blackwater", "the Unlucky",
                                                       "Ash-Tongue", "Red-Knife", "the Gaunt", "Cold-Heart", "Iron-Jaw",
                                                       "the Quiet", "Wolfsbane", "Grey-Cloak", "Coin-Biter"};
        const auto first = female ? women[detail::Mix(seed, 41u) % std::size(women)] : men[detail::Mix(seed, 41u) % std::size(men)];
        const auto by = bynames[detail::Mix(seed, 43u) % std::size(bynames)];
        return std::string(first) + " " + std::string(by);
    }

    // The reward, fixed when the quest is offered, by kind and the player's level.
    inline std::uint32_t RewardGold(Template t, int playerLevel) {
        const int base = t == Template::Keepsake ? 150 : t == Template::Bounty ? 250 : t == Template::Venture ? 200 : 100;
        const float scale = 1.0f + std::clamp(playerLevel, 1, 81) / 20.0f;
        return static_cast<std::uint32_t>(std::lround(base * scale / 5.0f) * 5);
    }

    // A name as people use it before "'s": "Uthgerd the Unbroken" is Uthgerd, "Olfina Gray-Mane" stays as she is.
    inline std::string ShortName(std::string_view name) {
        const auto the = name.find(" the ");
        if (the != std::string_view::npos && the > 0) return std::string(name.substr(0, the));
        return std::string(name);
    }

    // ------------------------------------------------------------------ words, from the record
    //
    // {player}, {giver}, {foe}, {ally}, {companion}, {place}, {item} (the stamped name), {third}, {gold} are filled
    // in by the bridge. Facts in the past or as they stand, never instructions on what to say.

    inline std::string Fill(std::string text, std::string_view key, const std::string& value) {
        for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
            text.replace(at, key.size(), value);
        }
        return text;
    }

    // The journal title of a quest, when the esp gives none.
    inline std::string DefaultTitle(Template t) {
        switch (t) {
            case Template::Keepsake: return "{item}";
            // The brigand's name is in the title: the journal's alias names read a spawned actor's template ("Bandit
            // Chief"), not the name given at runtime (2026-11-18), so the objective and log say "the brigand".
            case Template::Bounty: return "Bounty: {foe}";
            case Template::Venture: return "Into {place} with {ally}";
            default: return "A Request";
        }
    }

    // What the giver says when they come (the story visit's narration: what drives them to speak).
    inline std::string OfferNarration(const Quest& q) {
        switch (q.tmpl) {
            case Template::Keepsake:
                return "{giver} has come to ask {player} for help. Someone stole {their} {itembase} from {their} home and was "
                       "seen running toward {place}; {giver} wants it back and offers {gold} gold.";
            case Template::Bounty:
                return "{giver} has come to ask {player} for help. A brigand called {foe} and a gang have settled at {place} and "
                       "prey on everyone near it. {giver} wants them dealt with and offers {gold} gold.";
            case Template::Venture:
                return "{ally} is on the way to {place} to clear it out{withcompanion}, sees {player} and asks {player} to come "
                       "along and share what they find.";
            default: return {};
        }
    }

    // The text of the offer popup (the player's choice).
    inline std::string OfferPrompt(const Quest& q) {
        switch (q.tmpl) {
            case Template::Keepsake: return "{giver} asks you to recover {their} stolen {itembase} from {place}. Reward: {gold} gold.";
            case Template::Bounty: return "{giver} asks you to deal with {foe} at {place}. Reward: {gold} gold.";
            case Template::Venture:
                // What happens either way, said plainly (the user, 2026-11-18: they follow you if you accept, otherwise
                // they go alone).
                if (q.placed & Placed::AtPlace) {
                    // Met at the place itself: they are about to go in, not on their way.
                    return q.companion ? "{ally} and {companion} are about to clear out {place} and ask you to come in with them. "
                                         "Say yes and they follow you in and share what you find; say no and they go in alone."
                                       : "{ally} is about to clear out {place} and asks you to come in too. Say yes and {ally} "
                                         "follows you in and shares what you find; say no and {ally} goes in alone.";
                }
                return q.companion ? "{ally} and {companion} are on their way to clear out {place} and ask you to come along. "
                                     "Say yes and they follow you there and share what you find; say no and they go alone."
                                   : "{ally} is on the way to clear out {place} and asks you to come along. Say yes and {ally} "
                                     "follows you there and shares what you find; say no and {ally} goes alone.";
            default: return {};
        }
    }

    // What the giver (or the ally) knows of the quest now, first person, for their prompt. Empty = nothing.
    inline std::string GiverLines(const Quest& q, bool playerHasItem) {
        std::string out;
        switch (q.tmpl) {
            case Template::Keepsake:
                if (q.phase == Phase::Planned) {
                    out = "My {itembase} was stolen from my home, and the thief was seen running toward {place}. I am going to ask "
                          "{player} to get it back for {gold} gold.";
                } else if (q.phase == Phase::Open) {
                    // What is true, said plainly, so a claim in conversation cannot make it otherwise: a weak model
                    // paid Hulda's bounty in words on the player's say-so (2026-11-18).
                    out = "I asked {player} to get back my {itembase}, stolen from my home; the thief ran toward {place}. I "
                          "offered {gold} gold, paid when the {itembase} is back in my hands. It is not back in my hands yet, "
                          "and I have paid nothing.";
                    if (playerHasItem) out += " {player} has it with them right now.";
                }
                break;
            case Template::Bounty:
                if (q.phase == Phase::Planned) {
                    out = "A brigand called {foe} and a gang have settled at {place}. I am going to ask {player} to deal with them "
                          "for {gold} gold.";
                } else if (q.phase == Phase::Open) {
                    out = "I asked {player} to deal with {foe} and the gang at {place}, for {gold} gold, paid once {foe} is dead.";
                    out += (q.doneMask & 1) ? " Word has reached me that {foe} is dead. I owe {player} the {gold} gold and have not paid it yet."
                                            : " {foe} is alive: word of a death like that would reach me at once, and none has. I have paid nothing.";
                }
                break;
            case Template::Venture:
                if (q.phase == Phase::Planned) {
                    // Met at the place: they are there to clear it, and it is not cleared yet. Without this, Jenassa
                    // told the player "The mine is clear" outside Embershard with its master alive inside (2026-11-18).
                    out = (q.placed & Placed::AtPlace)
                              ? "I came to {place}{withcompanion} to clear it out. It is not cleared yet: whoever holds it is still inside."
                              : "I am on my way to {place}{withcompanion} to clear it out.";
                    // Once they have met the player: they may ask them along, and the asking is an action, so the
                    // offer the player answers comes when they really ask (the user, 2026-11-18).
                    if (q.placed & Placed::Met) {
                        out += " I ran into {player} on the way, and I would gladly have {player} come along and share whatever we "
                               "find. When I ask {player} to come along, I choose the InviteAlong action in that same reply; when "
                               "{player} asks to come along and I agree, I choose the TakePlayerAlong action in that same reply.";
                    }
                } else if (q.phase == Phase::Open) {
                    out = "{player} came along with me to {place}{withcompanion}, and I follow {player} there as a companion until it is done.";
                    if (q.doneMask & 0b10) out += " We cleared it together; we have yet to share what we found.";
                    else if (q.doneMask & 1) out += " We are there now and it is not cleared yet.";
                    else out += " We are not there yet.";
                }
                break;
            default: break;
        }
        return out;
    }

    // The giver's (or ally's) memory of how it ended, first person. Empty = nothing to remember.
    inline std::string EndMemory(const Quest& q) {
        switch (q.tmpl) {
            case Template::Keepsake:
                switch (q.ending) {
                    case Ending::Done: return "I asked {player} to get back my stolen {itembase} from {place}, and {player} brought it back to me. I paid {player} the {gold} gold I had offered.";
                    case Ending::Kept: return "I asked {player} to get back my stolen {itembase} from {place}. {player} found it and never brought it back to me.";
                    case Ending::Ignored: return "I asked {player} to get back my stolen {itembase} from {place}, but {player} never did.";
                    case Ending::Declined: return "I asked {player} to get back my stolen {itembase}, and {player} refused.";
                    default: return {};
                }
            case Template::Bounty:
                switch (q.ending) {
                    case Ending::Done: return "I asked {player} to deal with {foe} and the gang at {place}, and {player} did. I paid {player} the {gold} gold I had offered.";
                    case Ending::Ignored: return "I asked {player} to deal with {foe} and the gang at {place}, but {player} never did.";
                    case Ending::Declined: return "I asked {player} to deal with {foe} and the gang at {place}, and {player} refused.";
                    default: return {};
                }
            case Template::Venture:
                switch (q.ending) {
                    case Ending::Done: return "{player} and I cleared {place} together{withcompanion}, and we shared what we found.";
                    case Ending::WentAlone: return "I asked {player} to come along to {place}, but {player} did not come, so I went on my own.";
                    case Ending::Declined: return "I asked {player} to come along to {place}, and {player} said no, so I went on my own.";
                    default: return {};
                }
            default: return {};
        }
    }

    // A venture's companion (another former follower, or a follower type the player never travelled with): what they
    // know, first person. Empty = nothing.
    inline std::string CompanionLines(const Quest& q) {
        if (q.tmpl != Template::Venture || !q.companion) return {};
        if (q.phase == Phase::Planned) {
            std::string out = "I fell in with {ally} a few days ago, and we are on our way to {place} to clear it out.";
            if (q.placed & Placed::Met) {
                out += " We ran into {player} on the way; {ally} knows {player}. When I ask {player} to come along with us, I "
                       "choose the InviteAlong action in that same reply; when {player} asks to come along and I agree, I choose "
                       "the TakePlayerAlong action in that same reply.";
            }
            return out;
        }
        if (q.phase == Phase::Open) {
            std::string out = "{player} came along with {ally} and me to {place}.";
            if (q.doneMask & 0b10) out += " We cleared it together; we have yet to share what we found.";
            else if (q.doneMask & 1) out += " We are there now and it is not cleared yet.";
            else out += " We are not there yet.";
            return out;
        }
        switch (q.ending) {
            case Ending::Done: return "{ally}, {player} and I cleared {place} together, and we shared what we found.";
            case Ending::WentAlone:
            case Ending::Declined: return "{ally} asked {player} to come along to {place}; {player} did not, so {ally} and I went on our own.";
            default: return {};
        }
    }

    // What the two of a venture remember of how they came to travel together, made true when they are met: written
    // for the ally (about the companion) and the companion (about the ally).
    inline std::string FellInMemory(bool forCompanion) {
        return forCompanion ? "I fell in with {ally} a few days ago, and we set out together to clear out {place}."
                            : "I fell in with {companion} a few days ago, and we set out together to clear out {place}.";
    }

    // What the people of the giver's hold heard (SkyrimNet world knowledge), past tense so it stays true. While
    // open: the rumor of the trouble itself; ended: how it ended. Empty = nothing.
    inline std::string Rumor(const Quest& q) {
        if (q.phase == Phase::Open) {
            if (q.tmpl == Template::Keepsake) return "{givershort}'s {itembase} was stolen, and the thief was seen running toward {place}.";
            if (q.tmpl == Template::Bounty) return "A brigand called {foe} and a gang have settled at {place} and prey on travellers.";
            return {};
        }
        if (q.phase != Phase::Ended) return {};
        if (q.tmpl == Template::Keepsake && q.ending == Ending::Done) return "{player} brought {givershort}'s stolen {itembase} back from {place}.";
        if (q.tmpl == Template::Bounty && q.ending == Ending::Done) return "{player} dealt with {foe} and the gang at {place}.";
        if (q.tmpl == Template::Bounty && q.ending == Ending::Ignored) return "{foe} and the gang still hold {place}; {giver} asked {player} to deal with them, and nothing came of it.";
        if (q.tmpl == Template::Venture && q.ending == Ending::Done) return "{ally} and {player} cleared {place} together.";
        if (q.tmpl == Template::Venture && (q.ending == Ending::WentAlone || q.ending == Ending::Declined)) return "{ally} went to clear {place} alone.";
        return {};
    }

    // ------------------------------------------------------------------ save data (co-save record 'IEQS')

    constexpr std::uint32_t kRecordType = 'IEQS';
    constexpr std::uint32_t kRecordVersion = 1;

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
        inline void PutText(std::vector<std::uint8_t>& out, const std::string& text) {
            const auto size = static_cast<std::uint16_t>(std::min(text.size(), kMaxNote));
            Put(out, size);
            out.insert(out.end(), text.begin(), text.begin() + size);
        }
        inline bool GetText(std::span<const std::uint8_t> in, std::size_t& at, std::string& text) {
            std::uint16_t size = 0;
            if (!Get(in, at, size) || size > kMaxNote || in.size() - at < size) return false;
            text.assign(reinterpret_cast<const char*>(in.data() + at), size);
            at += size;
            return true;
        }
    }

    inline std::vector<std::uint8_t> Serialize(const Book& book) {
        std::vector<std::uint8_t> out;
        detail::Put(out, book.nextId);
        detail::Put(out, static_cast<std::uint8_t>(book.lastTemplate));
        detail::Put(out, book.lastSlot);
        const auto count = static_cast<std::uint32_t>(std::min(book.all.size(), kMaxKept));
        detail::Put(out, count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& q = book.all[i];
            detail::Put(out, q.id);
            detail::Put(out, static_cast<std::uint8_t>(q.tmpl));
            detail::Put(out, static_cast<std::uint8_t>(q.truth));
            detail::Put(out, static_cast<std::uint8_t>(q.phase));
            detail::Put(out, static_cast<std::uint8_t>(q.ending));
            detail::Put(out, q.slot);
            detail::Put(out, q.step);
            detail::Put(out, q.doneMask);
            detail::Put(out, q.placed);
            detail::Put(out, static_cast<std::uint8_t>(q.handed ? 1 : 0));
            detail::Put(out, q.rumor);
            for (const auto form : {q.giver, q.foe, q.ally, q.companion, q.place, q.anchor, q.item, q.third, q.holder}) detail::Put(out, form);
            detail::Put(out, q.gold);
            detail::Put(out, q.seed);
            detail::Put(out, q.created);
            detail::Put(out, q.opened);
            detail::Put(out, q.since);
            detail::Put(out, q.deadline);
            detail::PutText(out, q.stamp);
            detail::PutText(out, q.foeName);
            detail::PutText(out, q.title);
        }
        return out;
    }

    // All or nothing.
    inline bool Deserialize(std::span<const std::uint8_t> in, Book& book, std::uint32_t version = kRecordVersion) {
        if (version != 1) return false;
        std::size_t at = 0;
        Book read;
        std::uint8_t lastTemplate = 0;
        std::uint32_t count = 0;
        if (!detail::Get(in, at, read.nextId) || !detail::Get(in, at, lastTemplate) || !detail::Get(in, at, read.lastSlot) ||
            !detail::Get(in, at, count) || count > kMaxKept || lastTemplate > 3) {
            return false;
        }
        read.lastTemplate = static_cast<Template>(lastTemplate);
        read.all.resize(count);
        for (auto& q : read.all) {
            std::uint8_t tmpl = 0, truth = 0, phase = 0, ending = 0, handed = 0;
            if (!detail::Get(in, at, q.id) || !detail::Get(in, at, tmpl) || !detail::Get(in, at, truth) || !detail::Get(in, at, phase) ||
                !detail::Get(in, at, ending) || !detail::Get(in, at, q.slot) || !detail::Get(in, at, q.step) ||
                !detail::Get(in, at, q.doneMask) || !detail::Get(in, at, q.placed) || !detail::Get(in, at, handed) ||
                !detail::Get(in, at, q.rumor)) {
                return false;
            }
            for (auto* form : {&q.giver, &q.foe, &q.ally, &q.companion, &q.place, &q.anchor, &q.item, &q.third, &q.holder}) {
                if (!detail::Get(in, at, *form)) return false;
            }
            if (!detail::Get(in, at, q.gold) || !detail::Get(in, at, q.seed) || !detail::Get(in, at, q.created) ||
                !detail::Get(in, at, q.opened) || !detail::Get(in, at, q.since) || !detail::Get(in, at, q.deadline) ||
                !detail::GetText(in, at, q.stamp) || !detail::GetText(in, at, q.foeName) || !detail::GetText(in, at, q.title)) {
                return false;
            }
            const bool endingOk = ending == 0 || (ending >= 10 && ending <= 17);
            if (tmpl < 1 || tmpl > 3 || truth > 6 || phase > 2 || !endingOk || (q.slot != 0xFF && q.slot >= kSlots) ||
                q.step > StepsOf(static_cast<Template>(tmpl)).size() || !std::isfinite(q.deadline)) {
                return false;
            }
            q.tmpl = static_cast<Template>(tmpl);
            q.truth = static_cast<Truth>(truth);
            q.phase = static_cast<Phase>(phase);
            q.ending = static_cast<Ending>(ending);
            q.handed = handed != 0;
        }
        if (at != in.size()) return false;
        book = std::move(read);
        return true;
    }
}
