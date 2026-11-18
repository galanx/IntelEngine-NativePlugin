#pragma once

// IntelEngine story arcs (docs/STORY_ARCS.md): stories of several steps whose people are real people of the world,
// whose clues are real items in real pockets and homes, and whose branches are decided by facts of the game and by
// the player's choices, never by what anyone says. Pure logic without game types, checked offline by
// SKSE/tests/ArcsCheck.cpp; ArcBridge casts the arcs, reads the facts and carries out what the story needs, and
// IntelEngine_Arcs.psc shows the journal and the choices.
//
// An arc is a pattern (authored data: roles, props, nodes, edges, endings, the words each role knows), a cast of real
// actors chosen by the engine, props stamped with a unique name, and a record of where the story stands. The
// dialogue model writes only words, from what each role's lines say is true and not yet true.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IntelEngine::Arcs {

    enum class Pattern : std::uint8_t {
        None = 0,
        Thief = 1,   // crime: something dear to the asker was stolen by someone of the town; a witness saw them
        Feud = 2,    // drama: two people of a town at odds; settle it with fists, make peace, or stay out of it
        Murder = 3,  // mystery: someone of the town found dead; a witness, suspects with alibis, proof in a home
    };
    constexpr std::uint8_t kPatterns = 3;

    // Who is who (indices into Arc::cast; 0 is never used).
    enum Role : std::uint8_t {
        RNone = 0,
        RAsker = 1,     // who asks the player (the robbed, one side of a feud, the captain or a relative)
        RCulprit = 2,   // the thief, the killer
        RWitness = 3,   // who saw something
        RRival = 4,     // the other side of a feud
        RVictim = 5,    // who was killed
        RSuspect2 = 6,  // an innocent suspect
        RSuspect3 = 7,  // another innocent suspect
    };
    constexpr std::size_t kRoles = 8;
    constexpr std::size_t kProps = 3;

    enum class Kind : std::uint8_t {
        Talk = 1,    // the player is with `who` while they speak
        Have = 2,    // prop `prop` is in the player's pack
        Give = 3,    // prop `prop` was handed to `who` (a popup; the engine moves it)
        Choose = 4,  // the player answers a popup at `who`
        Brawl = 5,   // a brawl with `who`, won or lost
        Reach = 6,   // the player is where `who` lives (or died)
    };

    enum class Phase : std::uint8_t { Planned = 0, Open = 1, Ended = 2 };

    enum class Ending : std::uint8_t {
        None = 0,
        Done = 10,          // put right: the thief caught or forgiven and the thing returned, the feud settled, the killer named
        Failed = 11,        // the player tried and it went wrong (lost the brawl)
        Declined = 12,      // the player said no when it was offered
        Ignored = 13,       // the time ran out
        AskerDied = 14,     // the one who asked is dead
        WrongAccused = 15,  // the player named an innocent as the killer
        Kept = 16,          // the player found the stolen thing and kept it
        Lapsed = 17,        // never offered: nobody remembers anything
        Settled = 18,       // a feud made up without blows
        StayedOut = 19,     // the player chose to stay out of it
    };

    // Game work the bridge carries out when an edge fires.
    enum class Effect : std::uint8_t {
        None = 0,
        PayGold = 1,     // the asker pays the player the reward
        Jail = 2,        // the role is taken by the guard (away for a while, off-screen)
        Shame = 3,       // the role was wrongly accused: the town hears it
        StartBrawl = 4,  // a brawl between the player and the role begins
        PlayerPays = 5,  // the player pays the reward amount to the role (a feud bought off)
    };

    enum class If : std::uint8_t {
        Done = 1,     // the node's own fact holds
        Option0 = 2,  // the popup's first, second, third answer
        Option1 = 3,
        Option2 = 4,
        Picked = 5,   // the popup's answer named `role`
        Dead = 6,     // `role` is dead
        Has = 7,      // prop `prop` is in the player's pack
        Won = 8,      // the brawl was won
        Lost = 9,     // the brawl was lost
        Handed = 10,  // prop `prop` was handed to `role` (the hand-over popup, offered on this node too)
        Persuaded = 11,  // the first answer, and the player's Speech is enough to carry it (kPersuadeSpeech)
    };
    constexpr int kPersuadeSpeech = 35;

    constexpr std::uint8_t kNoNode = 0xFF;

    struct Edge {
        If when{};
        Role role = RNone;           // Picked, Dead: who
        std::uint8_t prop = 0;       // Has: which prop
        std::uint8_t needProps = 0;  // Picked: the player must carry one of these props (bit per prop), else this edge is skipped
        std::uint8_t next = kNoNode;
        Ending ending = Ending::None;
        Effect effect = Effect::None;
        Role effectRole = RNone;
        std::uint32_t setFlags = 0;
        std::uint16_t log = 0;       // the journal stage this branch writes (else the node's logStage)
        bool skip = false;           // the node's own fact did not happen (a shortcut): its objective is taken away,
                                     // not completed, and it writes no line of its own
        Effect effect2 = Effect::None;  // a second piece of work (the asker pays the reward after the player paid)
        Role effect2Role = RNone;
    };

    // The journal phrasings of an arc's journal quest (esp/EspBuilder ArcPhrasings, same order).
    namespace Objective {
        constexpr std::uint8_t AskWitness = 0;    // Ask <Witness> what they saw
        constexpr std::uint8_t FindStolen = 1;    // Find what was stolen from <Asker>
        constexpr std::uint8_t Confront = 2;      // Confront <Culprit>
        constexpr std::uint8_t Return = 3;        // Return what was stolen to <Asker>
        constexpr std::uint8_t HearRival = 4;     // Hear <Rival>'s side
        constexpr std::uint8_t DecideFeud = 5;    // Decide how to settle it between <Asker> and <Rival>
        constexpr std::uint8_t BeatRival = 6;     // Beat <Rival> in a brawl
        constexpr std::uint8_t MakePeace = 7;     // Make peace between <Asker> and <Rival>
        constexpr std::uint8_t SeeScene = 8;      // See where <Victim> died
        constexpr std::uint8_t Question = 9;      // Question <Witness>
        constexpr std::uint8_t FindProof = 10;    // Find proof of who killed <Victim>
        constexpr std::uint8_t NameKiller = 11;   // Name the killer to <Asker>
        constexpr std::uint8_t Count = 12;
    }

    struct Node {
        Kind kind{};
        Role who = RNone;
        std::uint8_t prop = 0;
        std::uint8_t objective = 0;
        Role marker = RNone;           // where the journal marker points (RNone: none)
        std::uint16_t logStage = 0;    // the journal stage set when the node is done (its log entry), 0 = none
        float extendDays = 0.0f;       // added to the deadline when the node is left
        std::array<const char*, 3> options{};  // Choose: the answers (with {placeholders}); nullptr = none
        std::array<Role, 3> optionRoles{};     // Choose: the role an answer names (accusations), RNone = none
        std::array<Edge, 5> edges{};           // looked at in order; the first that holds fires
    };

    // ------------------------------------------------------------------ the patterns

    namespace Flag {
        constexpr std::uint32_t WitnessHeard = 1u << 0;   // the witness told the player what they saw
        constexpr std::uint32_t Reported = 1u << 1;       // the thief was handed to the guard
        constexpr std::uint32_t Spared = 1u << 2;         // the thief was let go
        constexpr std::uint32_t HeardRival = 1u << 3;     // the rival told their side
        constexpr std::uint32_t Fists = 1u << 4;          // the feud goes to a brawl
        constexpr std::uint32_t Peace = 1u << 5;          // the feud goes to peace talk
        constexpr std::uint32_t SawScene = 1u << 6;       // the player saw where the victim died
        constexpr std::uint32_t NoProof = 1u << 7;        // the player named the right killer without proof
        constexpr std::uint32_t Apology = 1u << 8;        // peace through the asker's apology
        constexpr std::uint32_t BoughtOff = 1u << 9;      // peace bought with the player's gold
        constexpr std::uint32_t Refused = 1u << 10;       // the rival would not be talked round
    }

    struct PatternDef {
        Pattern pattern{};
        std::uint16_t base = 0;     // journal stages: base opens it, base + 10.. the nodes, base + 90 + n the endings
        float days = 0.0f;          // how long it stands once taken on
        std::span<const Node> nodes;
    };

    inline const PatternDef& Def(Pattern p) {
        // The thief among us. 0 ask the witness -> 1 find the thing -> 2 confront the thief -> 3 return it.
        static constexpr Node thief[] = {
            {Kind::Talk, RWitness, 0, Objective::AskWitness, RWitness, 1010, 0.0f, {}, {},
             {{{If::Has, RNone, 0, 0, 2, Ending::None, Effect::None, RNone, 0, 1020, true},
               {If::Done, RNone, 0, 0, 1, Ending::None, Effect::None, RNone, Flag::WitnessHeard}}}},
            {Kind::Have, RNone, 0, Objective::FindStolen, RCulprit, 1020, 0.0f, {}, {},
             {{{If::Done, RNone, 0, 0, 2, Ending::None, Effect::None, RNone, 0}}}},
            {Kind::Choose, RCulprit, 0, Objective::Confront, RCulprit, 0, 0.0f,
             {"Hand {culprit} over to the guard", "Let {culprit} go", nullptr}, {},
             {{{If::Dead, RCulprit, 0, 0, 3, Ending::None, Effect::None, RNone, 0, 1033, true},
               {If::Option0, RNone, 0, 0, 3, Ending::None, Effect::Jail, RCulprit, Flag::Reported, 1031},
               {If::Option1, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, Flag::Spared, 1032},
               {If::Handed, RAsker, 0, 0, kNoNode, Ending::Done, Effect::PayGold, RAsker, 0}}}},
            {Kind::Give, RAsker, 0, Objective::Return, RAsker, 0, 0.0f, {}, {},
             {{{If::Done, RNone, 0, 0, kNoNode, Ending::Done, Effect::PayGold, RAsker, 0}}}},
        };
        // A feud. 0 hear the rival -> 1 decide (fists, peace, stay out) -> 2 the brawl | 3 peace talk.
        static constexpr Node feud[] = {
            {Kind::Talk, RRival, 0, Objective::HearRival, RRival, 2010, 0.0f, {}, {},
             {{{If::Done, RNone, 0, 0, 1, Ending::None, Effect::None, RNone, Flag::HeardRival}}}},
            {Kind::Choose, RAsker, 0, Objective::DecideFeud, RAsker, 0, 0.0f,
             {"Settle it with your fists against {rival}", "Talk them into making peace", "Stay out of it"}, {},
             {{{If::Option0, RNone, 0, 0, 2, Ending::None, Effect::None, RNone, Flag::Fists, 2021},
               {If::Option1, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, Flag::Peace, 2022},
               {If::Option2, RNone, 0, 0, kNoNode, Ending::StayedOut, Effect::None, RNone, 0}}}},
            {Kind::Brawl, RRival, 0, Objective::BeatRival, RRival, 0, 0.0f, {}, {},
             {{{If::Won, RNone, 0, 0, kNoNode, Ending::Done, Effect::PayGold, RAsker, 0},
               {If::Lost, RNone, 0, 0, kNoNode, Ending::Failed, Effect::None, RNone, 0}}}},
            {Kind::Choose, RRival, 0, Objective::MakePeace, RRival, 0, 0.0f,
             {"Talk {rival} round (Speech)", "Pay {rival} {gold} gold to let it go", nullptr}, {},
             {{{If::Persuaded, RNone, 0, 0, kNoNode, Ending::Settled, Effect::PayGold, RAsker, Flag::Apology},
               {If::Option0, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, Flag::Refused, 2023},
               {If::Option1, RNone, 0, 0, kNoNode, Ending::Settled, Effect::PlayerPays, RRival, Flag::BoughtOff, 0, false,
                Effect::PayGold, RAsker}}}},
        };
        // Murder in town. 0 see the scene -> 1 question the witness -> 2 find proof -> 3 name the killer.
        static constexpr Node murder[] = {
            {Kind::Reach, RVictim, 0, Objective::SeeScene, RVictim, 3010, 0.0f, {}, {},
             {{{If::Has, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, 0, 3030, true},
               {If::Has, RNone, 1, 0, 3, Ending::None, Effect::None, RNone, 0, 3030, true},
               {If::Done, RNone, 0, 0, 1, Ending::None, Effect::None, RNone, Flag::SawScene}}}},
            {Kind::Talk, RWitness, 0, Objective::Question, RWitness, 3020, 0.0f, {}, {},
             {{{If::Has, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, 0, 3030, true},
               {If::Has, RNone, 1, 0, 3, Ending::None, Effect::None, RNone, 0, 3030, true},
               {If::Done, RNone, 0, 0, 2, Ending::None, Effect::None, RNone, Flag::WitnessHeard}}}},
            {Kind::Have, RNone, 0, Objective::FindProof, RNone, 3030, 0.0f, {}, {},
             {{{If::Has, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, 0},
               {If::Has, RNone, 1, 0, 3, Ending::None, Effect::None, RNone, 0}}}},
            {Kind::Choose, RAsker, 0, Objective::NameKiller, RAsker, 0, 0.0f,
             {"Accuse {pick0}", "Accuse {pick1}", "Accuse {pick2}"}, {RCulprit, RSuspect2, RSuspect3},
             {{{If::Picked, RCulprit, 0, 0b011, kNoNode, Ending::Done, Effect::Jail, RCulprit, 0},
               {If::Picked, RCulprit, 0, 0, 2, Ending::None, Effect::None, RNone, Flag::NoProof, 3031},
               {If::Picked, RSuspect2, 0, 0, kNoNode, Ending::WrongAccused, Effect::Shame, RSuspect2, 0},
               {If::Picked, RSuspect3, 0, 0, kNoNode, Ending::WrongAccused, Effect::Shame, RSuspect3, 0}}}},
        };
        static const PatternDef defs[] = {
            {Pattern::None, 0, 0.0f, {}},
            {Pattern::Thief, 1000, 4.0f, thief},
            {Pattern::Feud, 2000, 3.0f, feud},
            {Pattern::Murder, 3000, 5.0f, murder},
        };
        const auto i = static_cast<std::size_t>(p);
        return i < std::size(defs) ? defs[i] : defs[0];
    }

    constexpr std::uint16_t OpenStage(Pattern p) { return static_cast<std::uint16_t>(static_cast<int>(p) * 1000); }
    inline std::uint16_t EndStage(Pattern p, Ending e) {
        if (e == Ending::None || e == Ending::Declined || e == Ending::Lapsed) return 0;  // never reached the journal
        return static_cast<std::uint16_t>(OpenStage(p) + 90 + (static_cast<int>(e) - 10));
    }

    // ------------------------------------------------------------------ the record

    // What the game says right now (ArcBridge reads it, once per tick, in one call).
    struct Facts {
        std::array<bool, kRoles> with{};   // the player is with them (close, same space)
        std::array<bool, kRoles> spoke{};  // they spoke a moment ago
        std::array<bool, kRoles> dead{};
        std::array<bool, kProps> has{};    // the prop is in the player's pack
        std::array<bool, kProps> handed{}; // the prop went to the one the node gives it to
        bool atScene = false;              // the player is where `who` lives or died
        int answer = -1;                   // the popup's answer (shown index), -1 = none yet
        int speech = 0;                    // the player's Speech skill
        int brawl = 0;                     // 1 won, -1 lost, 0 none yet
    };

    constexpr std::size_t kMaxText = 200;

    struct Arc {
        std::uint32_t id = 0;
        Pattern pattern = Pattern::None;
        Phase phase = Phase::Planned;
        Ending ending = Ending::None;
        std::uint8_t slot = 0xFF;      // journal quest IntelEngine_Arc0..kSlots-1, 0xFF = none
        std::uint8_t node = 0;
        std::uint32_t visited = 0;     // nodes done (bit per node)
        std::uint32_t flags = 0;       // Flag:: bits
        std::array<std::uint32_t, kRoles> cast{};
        std::array<std::uint32_t, kProps> propBase{};  // the base item of each prop, 0 = none
        std::array<std::string, kProps> propName{};    // its stamped name ("Ysolda's Silver Necklace")
        std::array<std::uint32_t, kProps> propAt{};    // where it was put: an actor or a container, 0 = not yet
        std::array<Role, 3> order{RNone, RNone, RNone};  // Choose with roles: who each shown answer names
        std::uint32_t place = 0;       // the town (location) the arc happens in
        std::uint32_t scene = 0;       // a reference where `who` of a Reach node lives or died (a door, a bed, the body)
        std::uint32_t gold = 0;
        std::uint32_t paid = 0;        // gold the asker has already given the player since it opened (their own action)
        std::int32_t rumor = 0;        // the world knowledge the town heard of it (SkyrimNet's id), 0 = none
        std::uint32_t seed = 0;
        Role accused = RNone;          // who the player named, when they did
        float created = 0.0f;
        float opened = 0.0f;
        float since = 0.0f;
        float deadline = 0.0f;
        std::string title;
    };

    inline bool IsOpen(const Arc& a) { return a.phase == Phase::Open; }
    inline bool IsLive(const Arc& a) { return a.phase != Phase::Ended; }

    // What changed in one look: nodes done, the effect to carry out, and how it ended.
    struct Change {
        std::vector<std::uint8_t> done;
        Effect effect = Effect::None;
        Role effectRole = RNone;
        Ending ending = Ending::None;
        std::uint16_t log = 0;   // the journal stage to set for the step done (0 = none)
        std::uint8_t skipped = kNoNode;   // a node left by a shortcut: its objective is taken away
        Effect effect2 = Effect::None;
        Role effect2Role = RNone;
        bool stayed = false;     // a branch back to the same step fired (its line is written, nothing completes)
        bool any() const { return !done.empty() || stayed || skipped != kNoNode || ending != Ending::None || effect != Effect::None; }
    };

    // The role a shown answer names (accusations), RNone when the answer names nobody.
    inline Role AnswerRole(const Arc& a, const Node& n, int answer) {
        if (answer < 0 || answer > 2 || n.optionRoles[0] == RNone) return RNone;
        return a.order[static_cast<std::size_t>(answer)];
    }

    inline bool EdgeHolds(const Arc& a, const Node& n, const Edge& e, const Facts& f) {
        const auto has = [&](std::uint8_t mask) {
            for (std::size_t i = 0; i < kProps; ++i) if ((mask & (1u << i)) && f.has[i]) return true;
            return false;
        };
        switch (e.when) {
            case If::Done:
                switch (n.kind) {
                    case Kind::Talk: return f.with[n.who] && f.spoke[n.who];
                    case Kind::Have: return f.has[n.prop];
                    case Kind::Give: return f.handed[n.prop];
                    case Kind::Reach: return f.atScene;
                    default: return false;
                }
            case If::Option0: return f.answer == 0;
            case If::Option1: return f.answer == 1;
            case If::Option2: return f.answer == 2;
            case If::Picked:
                if (AnswerRole(a, n, f.answer) != e.role) return false;
                return e.needProps == 0 || has(e.needProps);
            case If::Dead: return e.role != RNone && f.dead[e.role];
            case If::Has: return f.has[e.prop];
            case If::Won: return f.brawl > 0;
            case If::Lost: return f.brawl < 0;
            case If::Handed: return f.handed[e.prop];
            case If::Persuaded: return f.answer == 0 && f.speech >= kPersuadeSpeech;
        }
        return false;
    }

    // Moves an open arc on from the facts: the asker's death first, then the current node's edges in order (the first
    // that holds fires), then the deadline. One node a look: an arc never jumps several steps on one tick, so every
    // step done is seen in the journal.
    inline Change Advance(Arc& a, const Facts& f, float now) {
        Change c;
        if (a.phase != Phase::Open) return c;
        const auto& def = Def(a.pattern);
        if (a.node >= def.nodes.size()) return c;
        auto end = [&](Ending e) {
            a.phase = Phase::Ended;
            a.ending = e;
            a.since = now;
            c.ending = e;
        };
        if (f.dead[RAsker]) {
            end(Ending::AskerDied);
            return c;
        }
        const Node& n = def.nodes[a.node];
        for (const auto& e : n.edges) {
            if (e.when == If{} || !EdgeHolds(a, n, e, f)) continue;
            a.visited |= 1u << a.node;
            a.flags |= e.setFlags;
            // A branch back to the same step (the rival would not be talked round) leaves it neither done nor
            // skipped: the player is still on it.
            const bool stays = e.next == a.node && e.ending == Ending::None;
            if (e.skip) c.skipped = a.node;
            else if (!stays) c.done.push_back(a.node);
            c.stayed = stays;
            c.effect = e.effect;
            c.effectRole = e.effectRole;
            c.log = e.log ? e.log : n.logStage;
            c.effect2 = e.effect2;
            c.effect2Role = e.effect2Role;
            a.deadline += n.extendDays;
            if (n.kind == Kind::Choose) a.accused = AnswerRole(a, n, f.answer);
            a.since = now;
            if (e.ending != Ending::None) {
                end(e.ending);
            } else if (e.next != kNoNode && e.next < def.nodes.size()) {
                a.node = e.next;
            }
            return c;
        }
        if (a.deadline > 0.0f && now >= a.deadline) {
            // The thief's prize found and never returned is kept; anything else simply ran out of time.
            if (a.pattern == Pattern::Thief && f.has[0]) end(Ending::Kept);
            else end(Ending::Ignored);
        }
        return c;
    }

    // ------------------------------------------------------------------ the book

    constexpr std::size_t kSlots = 4;      // journal quests IntelEngine_Arc0..3
    constexpr std::size_t kMaxOpen = 2;    // at most two arcs at once (with the quests, never a list of chores)
    constexpr std::size_t kMaxKept = 24;
    constexpr float kOfferDays = 1.0f;

    class Book {
    public:
        std::vector<Arc> all;
        std::uint32_t nextId = 1;
        Pattern lastPattern = Pattern::None;
        std::uint8_t lastSlot = 0xFF;

        std::size_t Count(Phase p) const {
            return static_cast<std::size_t>(std::count_if(all.begin(), all.end(), [p](const Arc& a) { return a.phase == p; }));
        }
        Arc* Find(std::uint32_t id) {
            for (auto& a : all) if (a.id == id) return &a;
            return nullptr;
        }
        const Arc* Find(std::uint32_t id) const {
            for (const auto& a : all) if (a.id == id) return &a;
            return nullptr;
        }
        // The live arc this person has a part in, nullptr = none.
        const Arc* LiveFor(std::uint32_t actor) const {
            if (!actor) return nullptr;
            for (const auto& a : all) {
                if (!IsLive(a)) continue;
                for (const auto who : a.cast) if (who == actor) return &a;
            }
            return nullptr;
        }
        // The role this person plays in a live arc, RNone = none.
        Role RoleOf(const Arc& a, std::uint32_t actor) const {
            for (std::size_t r = 1; r < kRoles; ++r) if (actor && a.cast[r] == actor) return static_cast<Role>(r);
            return RNone;
        }

        // Why an arc may not be planned now (empty = it may).
        std::string Refusal(const Arc& a) const {
            if (a.pattern == Pattern::None || !a.cast[RAsker]) return "nobody to ask";
            if (Count(Phase::Planned) > 0) return "another arc is on its way to the player";
            if (Count(Phase::Open) >= kMaxOpen) return "two arcs are open already";
            for (const auto who : a.cast) {
                if (who && LiveFor(who)) return "someone of the cast has a part in another arc";
            }
            if (a.pattern == lastPattern) return "the same kind of story ran last";
            return {};
        }

        std::uint32_t Plan(Arc a, float now) {
            if (!Refusal(a).empty()) return 0;
            a.id = nextId++;
            a.phase = Phase::Planned;
            a.ending = Ending::None;
            a.node = 0;
            a.visited = 0;
            a.slot = 0xFF;
            a.created = now;
            a.since = now;
            a.deadline = now + kOfferDays;
            lastPattern = a.pattern;
            all.push_back(std::move(a));
            Prune();
            return all.back().id;
        }

        bool Accept(std::uint32_t id, float now) {
            Arc* a = Find(id);
            if (!a || a->phase != Phase::Planned) return false;
            std::uint8_t pick = 0xFF;
            for (std::size_t n = 1; n <= kSlots; ++n) {
                const auto slot = static_cast<std::uint8_t>((static_cast<std::size_t>(lastSlot == 0xFF ? kSlots - 1 : lastSlot) + n) % kSlots);
                const bool used = std::any_of(all.begin(), all.end(), [slot](const Arc& o) { return IsOpen(o) && o.slot == slot; });
                if (!used) {
                    pick = slot;
                    break;
                }
            }
            if (pick == 0xFF) return false;
            for (auto& o : all) {
                if (o.id != id && o.slot == pick && !IsOpen(o)) o.slot = 0xFF;
            }
            a->slot = pick;
            lastSlot = pick;
            a->phase = Phase::Open;
            a->opened = now;
            a->since = now;
            a->deadline = now + Def(a->pattern).days;
            return true;
        }

        bool EndPlanned(std::uint32_t id, Ending ending, float now) {
            Arc* a = Find(id);
            if (!a || a->phase != Phase::Planned) return false;
            a->phase = Phase::Ended;
            a->ending = ending;
            a->since = now;
            return true;
        }

        std::vector<std::uint32_t> Lapse(float now) {
            std::vector<std::uint32_t> ids;
            for (auto& a : all) {
                if (a.phase == Phase::Planned && now >= a.deadline) {
                    a.phase = Phase::Ended;
                    a.ending = Ending::Lapsed;
                    a.since = now;
                    ids.push_back(a.id);
                }
            }
            return ids;
        }

        std::vector<std::uint32_t> OpenIds() const {
            std::vector<std::uint32_t> ids;
            for (const auto& a : all) if (IsOpen(a)) ids.push_back(a.id);
            return ids;
        }

    private:
        void Prune() {
            if (all.size() <= kMaxKept) return;
            std::stable_sort(all.begin(), all.end(), [](const Arc& x, const Arc& y) {
                if (IsLive(x) != IsLive(y)) return IsLive(x);
                return x.since > y.since;
            });
            all.resize(kMaxKept);
        }
    };

    // ------------------------------------------------------------------ the words

    namespace detail {
        inline std::uint32_t Mix(std::uint32_t a, std::uint32_t b) {
            std::uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
            h ^= h >> 15;
            h *= 0x2C1B3C6Du;
            h ^= h >> 12;
            return h;
        }
    }

    // The order the suspects are shown in, from the seed: never always the culprit first.
    inline std::array<Role, 3> ShuffleSuspects(std::uint32_t seed) {
        std::array<Role, 3> r{RCulprit, RSuspect2, RSuspect3};
        for (std::size_t i = 2; i > 0; --i) {
            const auto j = static_cast<std::size_t>(detail::Mix(seed, static_cast<std::uint32_t>(i)) % (i + 1));
            std::swap(r[i], r[j]);
        }
        return r;
    }

    // What a feud is about, told from each side (the same quarrel, each sure of their own right), chosen by the seed.
    struct Grievance {
        const char* asker;   // the asker's side, first person
        const char* rival;   // the rival's side, first person
        const char* heard;   // what the town says of it
    };
    inline const Grievance& FeudGrievance(std::uint32_t seed) {
        static constexpr Grievance list[] = {
            {"{rival} borrowed two hundred gold from me last harvest and swears it was paid back. It was not.",
             "{asker} says I still owe two hundred gold. I paid every coin back long ago, and {asker} knows it.",
             "{asker} and {rival} fell out over two hundred gold {rival} borrowed"},
            {"{rival} has been telling everyone at the inn that I cheat my customers.",
             "I told people at the inn what everyone whispers anyway: that {asker} cheats their customers. I will not take it back.",
             "{rival} has been telling the inn that {asker} cheats people"},
            {"{rival} insulted my family in front of half the town, and has never once said sorry.",
             "I said something about {asker}'s family in front of the town. It was the truth, and I will not apologise for the truth.",
             "{rival} insulted {asker}'s family in public"},
            {"{rival} cheated me at dice and laughed about it all over town.",
             "{asker} lost to me at dice, fair and square, and cannot stand losing.",
             "{asker} says {rival} cheated at dice"},
        };
        return list[seed % std::size(list)];
    }

    inline std::uint32_t RewardGold(Pattern p, int playerLevel) {
        const int base = p == Pattern::Murder ? 300 : p == Pattern::Thief ? 150 : 100;
        const int level = std::clamp(playerLevel, 1, 60);
        return static_cast<std::uint32_t>(base + level * 5);
    }

    inline std::string DefaultTitle(Pattern p) {
        switch (p) {
            case Pattern::Thief: return "A Thief Among Us";
            case Pattern::Feud: return "Bad Blood";
            case Pattern::Murder: return "Murder in {place}";
            default: return "A Story";
        }
    }

    // The offer's narration (what drives the asker to speak) and the popup.
    inline std::string OfferNarration(const Arc& a) {
        switch (a.pattern) {
            case Pattern::Thief:
                return "{asker} comes to {player} upset: someone broke into {asker}'s home and stole a {thing0}, and {asker} needs someone to find the thief";
            case Pattern::Feud:
                return std::string("{asker} comes to {player}, fed up with {rival}: ") + FeudGrievance(a.seed).asker +
                       " {asker} wants someone to settle the bad blood between them";
            case Pattern::Murder:
                return "{asker} comes to {player} grim-faced: {victim} was found dead, and {asker} wants someone to find out who killed them";
            default: return {};
        }
    }
    inline std::string OfferPrompt(const Arc& a) {
        switch (a.pattern) {
            case Pattern::Thief: return "Someone stole {item0}. {asker} asks you to find the thief and bring it back. Reward: {gold} gold.";
            case Pattern::Feud:
                return std::string("Bad blood: ") + FeudGrievance(a.seed).heard +
                       ". {asker} asks you to settle it with {rival}, one way or another. Reward: {gold} gold.";
            case Pattern::Murder: return "{asker} asks you to find out who killed {victim}, and name them with proof. Reward: {gold} gold.";
            default: return {};
        }
    }

    // What each role knows now, first person, for their prompt (the truth and what is not yet true). Empty = nothing.
    inline std::string RoleLines(const Arc& a, Role r) {
        const bool open = a.phase == Phase::Open;
        const bool planned = a.phase == Phase::Planned;
        std::string out;
        switch (a.pattern) {
            case Pattern::Thief:
                if (r == RAsker) {
                    if (planned) return "My {thing0} was stolen from my home. I am going to ask {player} to find the thief and bring it back, for {gold} gold.";
                    if (!open) return {};
                    out = "I asked {player} to find out who stole my {thing0} from my home and bring it back, for {gold} gold, paid when "
                          "{player} puts it back in my hands (saying it is found is not enough). It is not back in my hands yet. I do not know "
                          "who took it.";
                    out += a.paid ? " I have already paid {player} {paid} gold of it." : " I have paid nothing yet.";
                    if (a.flags & Flag::Reported) out += " {player} found that {culprit} took it and handed {culprit} to the guard.";
                    else if (a.flags & Flag::Spared) out += " {player} found that {culprit} took it, and let {culprit} go.";
                    return out;
                }
                if (r == RCulprit) {
                    if (!open && !planned) return {};
                    out = "I stole {asker}'s {thing0} from {asker}'s home. It is hidden among my things. Nobody must know; if anyone asks, I deny it.";
                    if (a.node >= 2 && !(a.visited & (1u << 2))) {
                        out += " {player} found it among my things and has it now, and knows it was me. I no longer have it.";
                    }
                    if (a.flags & Flag::Reported) {
                        out = "I stole {asker}'s {thing0}; {player} found it among my things, took it, and handed me to the guard. "
                              "I no longer have it.";
                    }
                    if (a.flags & Flag::Spared) {
                        out = "I stole {asker}'s {thing0}; {player} found it among my things, took it, and let me go. "
                              "I no longer have it. I owe {player} for that.";
                    }
                    return out;
                }
                if (r == RWitness) {
                    if (!open) return {};
                    return "The night {asker}'s {thing0} went missing from {asker}'s home I saw {culprit} slip out of there with something under their "
                           "arm. If anyone asks what I saw, I tell them.";
                }
                return {};
            case Pattern::Feud:
                if (r == RAsker) {
                    const std::string why = FeudGrievance(a.seed).asker;
                    if (planned) return why + " I have had enough of it. I am going to ask {player} to settle it, for {gold} gold.";
                    if (!open) return {};
                    out = why + " I asked {player} to settle it one way or another, for {gold} gold, paid once it is settled. "
                                "It is not settled yet, and I have paid nothing yet.";
                    if (a.flags & Flag::Fists) out += " {player} means to settle it with fists against {rival}; it is not done yet.";
                    if (a.flags & Flag::Peace) out += " {player} is trying to talk {rival} into making peace; it is not done yet.";
                    if (a.flags & Flag::Refused) out += " {rival} would not be talked round so far.";
                    return out;
                }
                if (r == RRival) {
                    if (!open && !planned) return {};
                    out = std::string(FeudGrievance(a.seed).rival) + " {asker} and I are at odds, and I will not be the one to back down.";
                    if (open) out += " {player} has taken it on, for {asker}. If {player} asks for my side of it, I give it.";
                    if (a.flags & Flag::Fists) out += " {player} wants to settle it with fists; if {player} challenges me, I accept.";
                    if (a.flags & Flag::Refused) out += " {player} tried to talk me round, and I would not have it.";
                    return out;
                }
                return {};
            case Pattern::Murder:
                if (r == RAsker) {
                    if (planned) return "{victim} was found dead at home. I am going to ask {player} to find out who did it, for {gold} gold.";
                    if (!open) return {};
                    out = "{victim} was found dead at home, killed. I asked {player} to find out who did it and name them to me with proof, "
                          "for {gold} gold. Nobody has been named yet, and I have paid nothing. I do not know who did it.";
                    if (a.flags & Flag::NoProof) out += " {player} named {culprit}, but brought me no proof; I will not take anyone on a word.";
                    return out;
                }
                if (r == RCulprit) {
                    if (!open) return {};
                    return "I killed {victim}, and hid the weapon at my home. Nobody saw me, I believe. If asked, I say I was at home "
                           "that night and never went near {victim}'s house; I never admit it.";
                }
                if (r == RWitness) {
                    if (!open) return {};
                    return "The night {victim} died I saw {culprit} near {victim}'s house, late, hurrying away. If anyone asks what I "
                           "saw, I tell them.";
                }
                if (r == RSuspect2 || r == RSuspect3) {
                    if (!open) return {};
                    return "{victim} and I had our quarrels, people know it, and they may think I did it. I did not. The night {victim} "
                           "died I was at the inn until late, and others saw me there; I say so if asked.";
                }
                return {};
            default:
                return {};
        }
    }

    // The memory each role keeps of how it ended, first person. Empty = nothing.
    inline std::string EndMemory(const Arc& a, Role r) {
        switch (a.pattern) {
            case Pattern::Thief:
                if (r == RAsker) {
                    if (a.ending == Ending::Done) return "My {thing0} was stolen from my home; {player} found {culprit} had taken it and brought it back to me. I paid {player} {gold} gold.";
                    if (a.ending == Ending::Kept) return "My {thing0} was stolen from my home; {player} found it and never gave it back to me.";
                    if (a.ending == Ending::Ignored) return "My {thing0} was stolen from my home; I asked {player} to find it, and {player} never did.";
                    if (a.ending == Ending::Declined) return "My {thing0} was stolen from my home; I asked {player} for help, and {player} refused.";
                }
                if (r == RCulprit && a.ending == Ending::Done) {
                    if (a.flags & Flag::Reported) return "I stole {asker}'s {thing0}; {player} found me out and handed me to the guard.";
                    if (a.flags & Flag::Spared) return "I stole {asker}'s {thing0}; {player} found me out and let me go.";
                    return "I stole {asker}'s {thing0}; {player} found it among my things and gave it back to {asker}. Nobody has come for "
                           "me over it.";
                }
                return {};
            case Pattern::Feud:
                if (r == RAsker) {
                    if (a.ending == Ending::Done) return "{player} settled my quarrel with {rival} with fists, and won. I paid {player} {gold} gold.";
                    if (a.ending == Ending::Failed) return "{player} took on {rival} in a brawl for me, and lost.";
                    if (a.ending == Ending::Settled) {
                        return (a.flags & Flag::BoughtOff)
                                   ? "{player} made peace between {rival} and me, paying {rival} off out of their own pocket. I paid {player} {gold} gold."
                                   : "{player} talked {rival} round and made peace between us. I paid {player} {gold} gold.";
                    }
                    if (a.ending == Ending::StayedOut) return "I asked {player} to settle my quarrel with {rival}; {player} chose to stay out of it.";
                }
                if (r == RRival) {
                    if (a.ending == Ending::Done) return "{player} beat me in a brawl over my quarrel with {asker}.";
                    if (a.ending == Ending::Failed) return "{player} challenged me over my quarrel with {asker}, and I won the brawl.";
                    if (a.ending == Ending::Settled) {
                        return (a.flags & Flag::BoughtOff) ? "{player} paid me to let my quarrel with {asker} go, and I did."
                                                           : "{player} talked me round, and I let my quarrel with {asker} go.";
                    }
                }
                return {};
            case Pattern::Murder:
                if (r == RAsker) {
                    if (a.ending == Ending::Done) return "{player} found out that {culprit} killed {victim}, and proved it. {culprit} was taken by the guard. I paid {player} {gold} gold.";
                    if (a.ending == Ending::WrongAccused) return "{player} named {accused} as {victim}'s killer, and was wrong. The killer is still free.";
                    if (a.ending == Ending::Ignored) return "I asked {player} to find {victim}'s killer, and nothing came of it.";
                }
                if (r == RCulprit && a.ending == Ending::Done) return "I killed {victim}; {player} found me out, and the guard took me.";
                if (a.ending == Ending::WrongAccused && r == a.accused) return "{player} named me as {victim}'s killer, before the whole town. I did not do it.";
                return {};
            default:
                return {};
        }
    }

    // What the town heard (past tense, stays true). Empty = nothing.
    inline std::string Rumor(const Arc& a) {
        if (a.phase == Phase::Open && a.pattern == Pattern::Murder) return "{victim} was found dead at home, killed by someone.";
        if (a.phase != Phase::Ended) return {};
        switch (a.pattern) {
            case Pattern::Thief:
                if (a.ending == Ending::Done && (a.flags & Flag::Reported)) return "{culprit} stole {asker}'s {thing0}; {player} found out and {culprit} was handed to the guard.";
                if (a.ending == Ending::Done) return "{asker}'s stolen {thing0} is back where it belongs.";
                return {};
            case Pattern::Feud:
                if (a.ending == Ending::Done) return "{player} beat {rival} in a brawl over {rival}'s quarrel with {asker}.";
                if (a.ending == Ending::Failed) return "{rival} beat {player} in a brawl over {rival}'s quarrel with {asker}.";
                if (a.ending == Ending::Settled) return std::string(FeudGrievance(a.seed).heard) + "; they have made peace since.";
                return {};
            case Pattern::Murder:
                if (a.ending == Ending::Done) return "{culprit} killed {victim}; {player} proved it, and {culprit} was taken by the guard.";
                if (a.ending == Ending::WrongAccused) return "{player} accused {accused} of killing {victim}; the killer has not been found.";
                if (a.ending == Ending::Ignored) return "Whoever killed {victim} has never been found.";
                return {};
            default:
                return {};
        }
    }

    inline std::string Fill(std::string text, std::string_view key, const std::string& value) {
        for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
            text.replace(at, key.size(), value);
        }
        return text;
    }

    // ------------------------------------------------------------------ save data (co-save record 'IEAR')

    constexpr std::uint32_t kRecordType = 'IEAR';
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
            const auto size = static_cast<std::uint16_t>(std::min(text.size(), kMaxText));
            Put(out, size);
            out.insert(out.end(), text.begin(), text.begin() + size);
        }
        inline bool GetText(std::span<const std::uint8_t> in, std::size_t& at, std::string& text) {
            std::uint16_t size = 0;
            if (!Get(in, at, size) || size > kMaxText || in.size() - at < size) return false;
            text.assign(reinterpret_cast<const char*>(in.data() + at), size);
            at += size;
            return true;
        }
    }

    inline std::vector<std::uint8_t> Serialize(const Book& book) {
        std::vector<std::uint8_t> out;
        detail::Put(out, book.nextId);
        detail::Put(out, static_cast<std::uint8_t>(book.lastPattern));
        detail::Put(out, book.lastSlot);
        const auto count = static_cast<std::uint32_t>(std::min(book.all.size(), kMaxKept));
        detail::Put(out, count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& a = book.all[i];
            detail::Put(out, a.id);
            detail::Put(out, static_cast<std::uint8_t>(a.pattern));
            detail::Put(out, static_cast<std::uint8_t>(a.phase));
            detail::Put(out, static_cast<std::uint8_t>(a.ending));
            detail::Put(out, a.slot);
            detail::Put(out, a.node);
            detail::Put(out, a.visited);
            detail::Put(out, a.flags);
            for (const auto who : a.cast) detail::Put(out, who);
            for (std::size_t p = 0; p < kProps; ++p) {
                detail::Put(out, a.propBase[p]);
                detail::Put(out, a.propAt[p]);
                detail::PutText(out, a.propName[p]);
            }
            for (const auto r : a.order) detail::Put(out, static_cast<std::uint8_t>(r));
            detail::Put(out, a.place);
            detail::Put(out, a.scene);
            detail::Put(out, a.gold);
            detail::Put(out, a.paid);
            detail::Put(out, a.rumor);
            detail::Put(out, a.seed);
            detail::Put(out, static_cast<std::uint8_t>(a.accused));
            detail::Put(out, a.created);
            detail::Put(out, a.opened);
            detail::Put(out, a.since);
            detail::Put(out, a.deadline);
            detail::PutText(out, a.title);
        }
        return out;
    }

    // All or nothing.
    inline bool Deserialize(std::span<const std::uint8_t> in, Book& book, std::uint32_t version = kRecordVersion) {
        if (version != 1) return false;
        std::size_t at = 0;
        Book read;
        std::uint8_t lastPattern = 0;
        std::uint32_t count = 0;
        if (!detail::Get(in, at, read.nextId) || !detail::Get(in, at, lastPattern) || !detail::Get(in, at, read.lastSlot) ||
            !detail::Get(in, at, count) || count > kMaxKept || lastPattern > kPatterns) {
            return false;
        }
        read.lastPattern = static_cast<Pattern>(lastPattern);
        read.all.resize(count);
        for (auto& a : read.all) {
            std::uint8_t pattern = 0, phase = 0, ending = 0, accused = 0;
            if (!detail::Get(in, at, a.id) || !detail::Get(in, at, pattern) || !detail::Get(in, at, phase) ||
                !detail::Get(in, at, ending) || !detail::Get(in, at, a.slot) || !detail::Get(in, at, a.node) ||
                !detail::Get(in, at, a.visited) || !detail::Get(in, at, a.flags)) {
                return false;
            }
            for (auto& who : a.cast) if (!detail::Get(in, at, who)) return false;
            for (std::size_t p = 0; p < kProps; ++p) {
                if (!detail::Get(in, at, a.propBase[p]) || !detail::Get(in, at, a.propAt[p]) || !detail::GetText(in, at, a.propName[p])) return false;
            }
            for (auto& r : a.order) {
                std::uint8_t v = 0;
                if (!detail::Get(in, at, v) || v >= kRoles) return false;
                r = static_cast<Role>(v);
            }
            if (!detail::Get(in, at, a.place) || !detail::Get(in, at, a.scene) || !detail::Get(in, at, a.gold) ||
                !detail::Get(in, at, a.paid) || !detail::Get(in, at, a.rumor) || !detail::Get(in, at, a.seed) || !detail::Get(in, at, accused) || !detail::Get(in, at, a.created) ||
                !detail::Get(in, at, a.opened) || !detail::Get(in, at, a.since) || !detail::Get(in, at, a.deadline) ||
                !detail::GetText(in, at, a.title)) {
                return false;
            }
            const bool endingOk = ending == 0 || (ending >= 10 && ending <= 19);
            if (pattern < 1 || pattern > kPatterns || phase > 2 || !endingOk || accused >= kRoles ||
                (a.slot != 0xFF && a.slot >= kSlots) || a.node >= Def(static_cast<Pattern>(pattern)).nodes.size() ||
                !std::isfinite(a.deadline)) {
                return false;
            }
            a.pattern = static_cast<Pattern>(pattern);
            a.phase = static_cast<Phase>(phase);
            a.ending = static_cast<Ending>(ending);
            a.accused = static_cast<Role>(accused);
        }
        if (at != in.size()) return false;
        book = std::move(read);
        return true;
    }
}
