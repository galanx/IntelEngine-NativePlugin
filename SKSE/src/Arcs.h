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
        Tale = 4,    // composed by the dungeon master (Tales.h): its own graph, people, words and endings
    };
    constexpr std::uint8_t kPatterns = 4;
    constexpr std::size_t kMaxTaleNodes = 12;        // a tale's steps (journal objectives kTaleObjective..+11)
    constexpr std::uint8_t kTaleObjective = 20;      // the first of the generic objectives whose text the DLL sets

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
    constexpr std::size_t kSites = 2;   // a tale's places out in the world (dungeons, ruins, camps)
    constexpr std::size_t kClues = 3;   // what a tale leaves out there to be looked at closely (a victim's remains, and the signs on the way: a trail)
    constexpr std::size_t kThugs = 3;   // the men of a tale's ambush

    enum class Kind : std::uint8_t {
        Talk = 1,    // the player is with `who` while they speak
        Have = 2,    // prop `prop` is in the player's pack
        Give = 3,    // prop `prop` was handed to `who` (a popup; the engine moves it)
        Choose = 4,  // the player answers a popup at `who`
        Brawl = 5,   // a brawl with `who`, won or lost
        Reach = 6,   // the player is where `who` lives (or died)
        Visit = 7,   // `who` comes to the player (the engine sends them), and they talk
        Wait = 8,    // time passes: `waitHours` game hours on this step
        Delve = 9,   // the player reaches the depths of place `site` (its boss room)
        Slay = 10,   // the boss of place `site` is dead
        Ambush = 11, // hired men sent by `who` (may be none) fall on the player out in the wilds: they are dead
        Stalk = 12,  // `who` shadows the player out of doors until caught or coming forward, and they talk
        Free = 13,   // `who` is held at place `site`: the player has reached them there, and they are free
        Examine = 14, // what was left of a victim lies outside place `site`: the player has looked at it closely (clue `prop`)
        Haunt = 15,   // something walks by night outside place `site`: it is gone, by force (Done) or at rest (Rested)
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
        Lost = 20,          // a tale: someone a step needed is dead, and the matter ends there
    };

    // Game work the bridge carries out when an edge fires.
    enum class Effect : std::uint8_t {
        None = 0,
        PayGold = 1,     // the asker pays the player the reward
        Jail = 2,        // the role is taken by the guard (away for a while, off-screen)
        Shame = 3,       // the role was wrongly accused: the town hears it
        StartBrawl = 4,  // a brawl between the player and the role begins
        PlayerPays = 5,  // the player pays the reward amount to the role (a feud bought off); a tale's price if it has one
        Leave = 6,       // the role leaves town for a while (off-screen, once unseen), then comes home
        Befriend = 7,    // the role and `bondWith` become friends (the game's relationship rank)
        Estrange = 8,    // the role and `bondWith` become rivals
        GiveItem = 9,    // the role hands prop `prop` (the edge's) to the player, from wherever it is
        Closer = 10,     // the role and the player grow closer by a step (the game's own rank, within friendship: Hearts.h)
        Apart = 11,      // the role and the player grow apart by a step
        Remedy = 12,     // the role gives the player what they have against what lives in a place (the edge's `prop` is
                         // the place's index): the worth of having named rightly what killed someone
    };
    constexpr std::uint8_t kEffects = 12;

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
        Persuaded = 11,  // the answer `prop` (the first by default), and the player's Speech is enough to carry it
        Said0 = 12,      // a talk's prepared outcome 0, 1, 2: what the conversation turned out to be (the judge's call)
        Said1 = 13,
        Said2 = 14,
        Waited = 15,     // `hours` game hours passed on this step
        Freed = 16,      // the one held is free already: the player found them before the story came to it
        Rested = 17,     // what walked by night was laid to rest: the player brought it what it wanted
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
        std::string line;            // a tale's journal line for this branch (first person, past tense)
        float hours = 0.0f;          // Waited: how long
        Role bondWith = RNone;       // Befriend, Estrange: with whom
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
        std::array<std::string, 3> options{};  // Choose: the answers (with {placeholders}); empty = none
        std::array<Role, 3> optionRoles{};     // Choose: the role an answer names (accusations), RNone = none
        std::array<Edge, 6> edges{};           // looked at in order; the first that holds fires
        // A tale's own words (patterns keep theirs in the esp and in the bridge).
        std::string goal;                      // the journal objective
        std::string why;                       // Visit: what brings the visitor, in their own terms (narration)
        std::string question;                  // Choose: the popup's question
        std::array<std::string, 3> turns{};    // Talk: what each prepared outcome would be (for the judge)
        float waitHours = 0.0f;                // Wait: how long
        std::uint8_t site = 0;                 // Delve, Slay: the place, index + 1 (0 = none)
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
        constexpr std::uint32_t CameFar = 1u << 11;       // the asker came to the player from another hold (rare: counted)
        constexpr std::uint32_t Deferred = 1u << 12;      // the asker could not reach the player (gone): at the next meeting
        constexpr std::uint32_t Heart = 1u << 13;         // a story of the heart between the asker and the player (never two running)
        constexpr std::uint32_t Rumored = 1u << 14;       // it began as talk in town: its asker did not come to the player at once
        constexpr std::uint32_t Sent = 1u << 15;          // the director's own rule let the asker come from elsewhere in the hold
    }

    struct PatternDef {
        Pattern pattern{};
        std::uint16_t base = 0;     // journal stages: base opens it, base + 10.. the nodes, base + 90 + n the endings
        float days = 0.0f;          // how long it stands once taken on
        std::span<const Node> nodes;
    };

    inline const PatternDef& Def(Pattern p) {
        // The thief among us. 0 ask the witness -> 1 find the thing -> 2 confront the thief -> 3 return it.
        static const Node thief[] = {
            {Kind::Talk, RWitness, 0, Objective::AskWitness, RWitness, 1010, 0.0f, {}, {},
             {{{If::Has, RNone, 0, 0, 2, Ending::None, Effect::None, RNone, 0, 1020, true},
               {If::Done, RNone, 0, 0, 1, Ending::None, Effect::None, RNone, Flag::WitnessHeard}}}},
            {Kind::Have, RNone, 0, Objective::FindStolen, RCulprit, 1020, 0.0f, {}, {},
             {{{If::Done, RNone, 0, 0, 2, Ending::None, Effect::None, RNone, 0}}}},
            {Kind::Choose, RCulprit, 0, Objective::Confront, RCulprit, 0, 0.0f,
             {"Hand {culprit} over to the guard", "Let {culprit} go", ""}, {},
             {{{If::Dead, RCulprit, 0, 0, 3, Ending::None, Effect::None, RNone, 0, 1033, true},
               {If::Option0, RNone, 0, 0, 3, Ending::None, Effect::Jail, RCulprit, Flag::Reported, 1031},
               {If::Option1, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, Flag::Spared, 1032},
               {If::Handed, RAsker, 0, 0, kNoNode, Ending::Done, Effect::PayGold, RAsker, 0}}}},
            {Kind::Give, RAsker, 0, Objective::Return, RAsker, 0, 0.0f, {}, {},
             {{{If::Done, RNone, 0, 0, kNoNode, Ending::Done, Effect::PayGold, RAsker, 0}}}},
        };
        // A feud. 0 hear the rival -> 1 decide (fists, peace, stay out) -> 2 the brawl | 3 peace talk.
        static const Node feud[] = {
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
             {"Talk {rival} round (Speech)", "Pay {rival} {gold} gold to let it go", ""}, {},
             {{{If::Persuaded, RNone, 0, 0, kNoNode, Ending::Settled, Effect::PayGold, RAsker, Flag::Apology},
               {If::Option0, RNone, 0, 0, 3, Ending::None, Effect::None, RNone, Flag::Refused, 2023},
               {If::Option1, RNone, 0, 0, kNoNode, Ending::Settled, Effect::PlayerPays, RRival, Flag::BoughtOff, 0, false,
                Effect::PayGold, RAsker}}}},
        };
        // Murder in town. 0 see the scene -> 1 question the witness -> 2 find proof -> 3 name the killer.
        static const Node murder[] = {
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
             {{{If::Picked, RCulprit, 0, 0b011, kNoNode, Ending::Done, Effect::Jail, RCulprit, 0, 0, false, Effect::PayGold, RAsker},
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

    // A tale's journal has three stages of its own: 10 the story so far, 90 a good end (completes), 91 a bad one (fails).
    constexpr std::uint16_t kTaleOpenStage = 10;
    constexpr std::uint16_t kTaleGoodStage = 90;
    constexpr std::uint16_t kTaleBadStage = 91;
    inline bool GoodEnding(Ending e) { return e == Ending::Done || e == Ending::Settled || e == Ending::StayedOut; }
    constexpr std::uint16_t OpenStage(Pattern p) {
        return p == Pattern::Tale ? kTaleOpenStage : static_cast<std::uint16_t>(static_cast<int>(p) * 1000);
    }
    inline std::uint16_t EndStage(Pattern p, Ending e) {
        if (e == Ending::None || e == Ending::Declined || e == Ending::Lapsed) return 0;  // never reached the journal
        if (p == Pattern::Tale) return GoodEnding(e) ? kTaleGoodStage : kTaleBadStage;
        return static_cast<std::uint16_t>(OpenStage(p) + 90 + (static_cast<int>(e) - 10));
    }
    // Which of a tale's three written endings an ending is: 0 the good one, 1 the bad one, 2 the other; -1 none of
    // them (time ran out, the asker died: the engine's own words).
    inline int TaleEndIndex(Ending e) {
        switch (e) {
            case Ending::Done: return 0;
            case Ending::Failed: return 1;
            case Ending::StayedOut: return 2;
            default: return -1;
        }
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
        int turn = -1;                     // a talk's outcome as the judge read the conversation, -1 = none yet
        float hoursHere = 0.0f;            // game hours on this step (Advance fills it in)
        std::array<bool, kSites> atDepths{};   // the player is in the boss room of the place
        std::array<bool, kSites> bossDead{};   // the place's boss is dead
        bool ambushOver = false;               // the men of the ambush are dead
        bool captiveReached = false;           // the player stands by the one held at a place, and nobody is fighting
        std::array<bool, kClues> examined{};   // the player has looked closely at what was left there
        std::uint8_t spirit = 0;               // what walks by night (SpiritState): SpiritBanished or SpiritRested once it is gone
    };

    constexpr std::size_t kMaxText = 200;
    // The same in the save record, in bytes. What the writer is allowed is counted in letters (Tales.h), and a letter
    // is up to four bytes: at 200 bytes a Russian goal of 140 letters lost its end at the first save, a Japanese one
    // two thirds of itself. Records written before hold shorter texts and load as they did.
    constexpr std::size_t kMaxTextBytes = 800;
    constexpr std::size_t kMaxLongBytes = 6000;
    constexpr std::size_t kMaxLong = 2400;   // a tale's longer texts: a person's truth, the story so far

    // How a tale ends, in its author's words: the journal's closing line, what the town hears (may be empty: a private
    // matter), what each person remembers (first person), and whether the asker pays the reward.
    struct TaleEnd {
        std::string journal;
        std::string rumor;
        std::array<std::string, kRoles> memory{};
        bool pay = false;
        // What this ending leaves unsettled among these people, in a sentence (someone let go who may come back, a
        // debt still owed, a truth one of them does not know yet); empty when it settles everything. A later story
        // of the town grows out of it (TaleAuthor: an open end that has come due).
        std::string after;
    };

    // Where someone held at a place stands: to be taken (out of anyone's sight, before the asker has come), away
    // until the player has come as far as the heart of the place, bound there, freed and with the player, and gone
    // their own way home. The script moves them and says so (ArcBridge::Captive); the step "free" makes them Freed.
    enum CaptiveState : std::uint8_t { CaptiveToTake = 0, CaptiveAway = 1, CaptiveBound = 2, CaptiveFreed = 3, CaptiveHome = 4 };

    // What was left of a victim, to be looked at closely: who or what lies there, and where the engine has it. The
    // script puts it down outside the place once the player has come that far (dead, where that can be helped out of
    // sight) and says so; the player's hand on it makes it Seen (ArcBridge::Activated), whichever step is current.
    enum Remains : std::uint8_t { RemainsNone = 0, RemainsHunter = 1, RemainsCow = 2, RemainsHorse = 3, RemainsGoat = 4, RemainsDeer = 5 };
    constexpr std::uint8_t kRemains = 5;
    enum ClueState : std::uint8_t { ClueToPlace = 0, CluePlaced = 1, ClueSeen = 2 };

    // Something that walks by night outside a place: the shade of someone who died there and cannot rest. It is out
    // only between nightfall and first light, and only while the story has not been past it; it harms nobody who
    // leaves it be, speaks from its own truth, and is gone for good when it is struck down (Banished) or given the
    // one thing it wants (Rested). The script puts it there and takes it away again (ArcBridge::SpiritOut / SpiritGone).
    enum SpiritState : std::uint8_t { SpiritAway = 0, SpiritOut = 1, SpiritBanished = 2, SpiritRested = 3 };
    constexpr float kNightBegins = 21.0f;   // the hour of the day
    constexpr float kNightEnds = 5.0f;
    inline bool IsNight(float hour) { return hour >= kNightBegins || hour < kNightEnds; }

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
        std::array<std::string, kProps> propText{};    // the words written in it when it is a paper (a letter, a ledger), else empty
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
        // ---- a tale's own (Pattern::Tale; empty for the patterns)
        std::vector<Node> graph;                       // its steps
        std::string genre;                             // crime, romance, drama... (the ledger's variety)
        std::string premise;                           // the hidden truth, in a sentence or two
        std::string offerWords;                        // what drives the asker to come (narration)
        std::string offerAsk;                          // the popup's offer, one sentence
        std::string opening;                           // the journal's first line (first person)
        std::array<std::string, kRoles> roleName{};    // what the author calls each person's part ("the lender")
        std::array<std::string, kRoles> truth{};       // each person's own truth, first person
        std::array<std::string, kRoles> heard{};       // what each person was there for, as it happened
        std::array<std::uint8_t, kProps> propHome{};   // the person (role) in whose home a prop is hidden, 0 = none
        std::array<std::uint8_t, kProps> propPocket{}; // the person who carries it, 0 = none
        std::array<TaleEnd, 3> ends{};                 // good, bad, other
        std::string story;                             // the story so far (the journal's log), first person
        std::uint32_t price = 0;                       // what the player pays when an answer costs gold, 0 = the reward
        float days = 0.0f;                             // how long it stands once taken on (0 = the pattern's)
        // ---- a tale's places out in the world (record version 4)
        std::array<std::uint32_t, kSites> site{};       // the place (a location), 0 = none
        std::array<std::uint32_t, kSites> siteAnchor{}; // its boss room (a reference)
        std::array<std::uint32_t, kSites> siteBoss{};   // the boss put there for the story, 0 = none (yet)
        std::array<std::uint8_t, kSites> siteKind{};    // what lives there (Tales::SiteKind), chooses the boss
        std::array<std::string, kSites> bossName{};     // the boss's name in the story ("Grelka the Silent"), may be empty
        std::array<std::uint8_t, kSites> siteState{};   // bit 0 the boss is placed, bit 1 its company placed
        std::array<std::uint8_t, kProps> propSite{};    // the place (index + 1) whose boss carries a prop, 0 = none
        // ---- an ambush on the road (record version 5)
        std::array<std::uint32_t, kThugs> thug{};       // the men placed for it, 0 = none (yet); thug[0] leads
        std::string thugName;                           // their leader's name in the story, may be empty
        std::uint8_t propAmbush = 0;                    // props (bit per prop) the leader carries
        // ---- someone held at a place (record version 7)
        std::uint8_t captive = 0;                       // who is held (a role), 0 = nobody
        std::uint8_t captiveSite = 0;                   // the place (index + 1) they are held at
        std::uint8_t captiveState = CaptiveToTake;
        // ---- what was left of a victim near a place, to be looked at closely (record version 8)
        std::array<std::uint8_t, kClues> clueWhat{};    // who or what lies there (Remains), 0 = no such clue
        std::array<std::uint8_t, kClues> clueSite{};    // the place (index + 1) it lies outside of
        std::array<std::uint32_t, kClues> clueRef{};    // what the script put there, 0 = nothing yet
        std::array<std::uint8_t, kClues> clueState{};   // ClueState
        // ---- a trail, and what a story leaves to the next (record version 10)
        std::array<std::uint8_t, kClues> clueFar{};     // how far out along the way to the place it lies: 0 by the place itself, 1, 2 farther
        std::uint32_t sequelOf = 0;                     // the earlier tale this one grows out of, 0 = none
        std::uint8_t followed = 0;                      // a later tale has taken up what this one left open
        // ---- something that walks by night outside a place (record version 9)
        std::uint8_t spiritSite = 0;                    // the place (index + 1) it walks outside of, 0 = nothing walks
        std::uint8_t spiritWoman = 0;                   // 1 a woman's shade, 0 a man's
        std::uint8_t spiritWants = 0;                   // the prop (index + 1) that lays it to rest, 0 = nothing does
        std::uint8_t spiritState = SpiritAway;
        std::uint32_t spiritRef = 0;                    // the shade the script put there, 0 = it is not out
        std::string spiritName;                         // its name in the story ("the Pale Widow")
        std::string spiritTruth;                        // who it was, how it died, what holds it here: first person
    };

    inline bool IsOpen(const Arc& a) { return a.phase == Phase::Open; }

    // ---- talk in town, and the one it is about
    // The user (2026-11-20): "hearing rumors about someone's problems, doesn't mean they should never approach you for
    // help. We cannot simply shut down a feature. And we may sometimes witness people asking around for help until
    // they reach you. [...] The rumors just make it more rare so it doesnt get repetetive and it doesnt look centered
    // around the player, but still, those things should also happen."
    // So the asker of a story that began as talk does not sit and wait for ever. Which of them do what is fixed by the
    // story's own seed (a save plays out one way):
    //  - when the player is near, they come over as before; about half of them first stop whoever stands near and
    //    ask them (AsksAround), so that the player sees someone looking for help, not someone looking for the player;
    //  - about one in three, once the talk has gone round for a good part of a day and nobody came, set out through
    //    their own town to find someone (SetsOut), and reach the player after asking on the way. By day only.
    namespace detail {
        inline std::uint32_t Mix(std::uint32_t a, std::uint32_t b);   // further down
    }
    constexpr float kSetOutAfterHours = 6.0f;
    constexpr float kSetOutFromHour = 8.0f, kSetOutUntilHour = 20.0f;
    inline bool AsksAround(const Arc& a) { return (a.flags & Flag::Rumored) != 0 && detail::Mix(a.seed, 0xA5CAu) % 100u < 50u; }
    inline bool SetsOut(const Arc& a) { return (a.flags & Flag::Rumored) != 0 && detail::Mix(a.seed, 0x5E70u) % 100u < 34u; }
    inline bool MaySetOutNow(const Arc& a, float now, float hour) {
        return SetsOut(a) && a.phase == Phase::Planned && now >= a.created && (now - a.created) * 24.0f >= kSetOutAfterHours &&
               hour >= kSetOutFromHour && hour < kSetOutUntilHour;
    }

    // Who goes to the player with their trouble, and who stays where they are until the player comes by. (The user,
    // 2026-11-21, of an innkeeper who walked out of her town to a farm to ask a favour of someone who had stood in her
    // inn for a minute: "he never met her before and she left her inn to track the player down as if she knows he is
    // some kind of hero".) In this order:
    //  - whoever the player is with may speak up there and then (a stranger with trouble takes a chance on a stranger);
    //  - who was let come from elsewhere by the director's own rule had a reason worth the road;
    //  - somebody with a post to keep (who sells, who guards, who holds office) does not leave it to look for anyone;
    //  - nobody goes out of their own town after the player: with the player beyond its houses, they wait;
    //  - a stranger does not go looking for the player, unless as someone asking round their town for anyone at all
    //    (SetsOut above, the rarer way of a story that began as talk).
    // Whoever stays waits (Flag::Deferred), their trouble is talk in town, and the offer goes out when the two meet.
    constexpr int kAcquaintedEvents = 6;    // SkyrimNet has seen the two have to do with each other this often: talk, not a greeting
    // Time actually played (LivingWorld.h says why): a town's own story begins after this much play in the town, and
    // not within this much play of the last story that began by itself, whatever the game's clock was turned to.
    constexpr float kTownLingerPlayed = 240.0f;   // four minutes (two game hours are six at the game's own pace)
    constexpr float kStoryGapPlayed = 1200.0f;    // twenty minutes (a game day is seventy-two)
    inline constexpr bool PlayedEnough(float playedSince, float needed) { return playedSince < 0.0f || playedSince >= needed; }
    constexpr float kWithPlayer = 3000.0f;  // "with the player": the same room, or out of doors this near
    constexpr float kTownReach = 4500.0f;   // an open town reaches this far round its marker on the map
    enum class Stays : std::uint8_t { No = 0, Post, OutOfTown, Stranger, Barred };
    struct Going {
        bool with = false;          // the player is with them
        bool sent = false;          // let come from elsewhere by the director's rule (Flag::CameFar, Flag::Sent)
        bool post = false;          // they have a post to keep
        bool inTown = false;        // the player is among the houses of their town
        int knows = 0;              // 2 has had to do with the player, 1 their town has heard of the player, 0 a stranger
        bool askingAround = false;  // they have set out to ask round their town for anyone
        bool barred = false;        // the player is where nobody is sent (another plane, a dungeon; Patrols::PlaceBars)
    };
    inline constexpr Stays StaysPut(const Going& g) {
        if (g.with) return Stays::No;
        // Not even somebody the director lets come from afar: there is no road to where the player is, or it leads
        // into a dungeon after them.
        if (g.barred) return Stays::Barred;
        if (g.sent) return Stays::No;
        if (g.post) return Stays::Post;
        if (!g.inTown) return Stays::OutOfTown;
        if (g.knows <= 0 && !g.askingAround) return Stays::Stranger;
        return Stays::No;
    }

    // The steps of an arc: a tale's own graph, else its pattern's table.
    inline std::span<const Node> NodesOf(const Arc& a) {
        if (!a.graph.empty()) return a.graph;
        return Def(a.pattern).nodes;
    }
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
        Role bondWith = RNone;   // Befriend, Estrange: with whom
        std::uint8_t effectProp = 0;   // GiveItem: which prop
        std::string line;        // a tale's journal line for the branch taken
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
                    case Kind::Visit: return f.with[n.who] && f.spoke[n.who];
                    case Kind::Stalk: return f.with[n.who] && f.spoke[n.who];
                    case Kind::Wait: return f.hoursHere >= n.waitHours;
                    case Kind::Delve: return n.site >= 1 && n.site <= kSites && f.atDepths[n.site - 1u];
                    case Kind::Slay: return n.site >= 1 && n.site <= kSites && f.bossDead[n.site - 1u];
                    case Kind::Ambush: return f.ambushOver;
                    case Kind::Free: return f.captiveReached;
                    case Kind::Examine: return n.prop < kClues && f.examined[n.prop];
                    case Kind::Haunt: return f.spirit == SpiritBanished;
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
            case If::Persuaded: return f.answer == static_cast<int>(e.prop) && f.speech >= kPersuadeSpeech;
            case If::Said0: return f.turn == 0;
            case If::Said1: return f.turn == 1;
            case If::Said2: return f.turn == 2;
            case If::Waited: return f.hoursHere >= e.hours;
            case If::Freed: return f.captiveReached;
            case If::Rested: return f.spirit == SpiritRested;
        }
        return false;
    }

    // Moves an open arc on from the facts: the asker's death first, then the current node's edges in order (the first
    // that holds fires), then the deadline. One node a look: an arc never jumps several steps on one tick, so every
    // step done is seen in the journal.
    // What the person of a tale's step saw happen there, for their own lines ({player} is filled in later).
    inline std::string Witnessed(const Node& n, const Edge& e, const Facts& f) {
        switch (e.when) {
            case If::Said0: case If::Said1: case If::Said2: {
                const auto k = static_cast<std::size_t>(static_cast<int>(e.when) - static_cast<int>(If::Said0));
                return n.turns[k].empty() ? std::string() : "When {player} talked with me about it: " + n.turns[k] + ".";
            }
            case If::Option0: case If::Option1: case If::Option2: case If::Persuaded: {
                const int k = e.when == If::Persuaded ? static_cast<int>(e.prop) : static_cast<int>(e.when) - static_cast<int>(If::Option0);
                if (k < 0 || k > 2 || n.options[static_cast<std::size_t>(k)].empty()) return {};
                return "{player} decided, in front of me: \"" + n.options[static_cast<std::size_t>(k)] + "\"" +
                       (e.when == If::Persuaded ? " and talked me round." : ".");
            }
            case If::Won: return "{player} beat me in a brawl over it.";
            case If::Lost: return "I beat {player} in a brawl over it.";
            case If::Handed: case If::Done:
                if (n.kind == Kind::Give) return "{player} brought it and gave it to me.";
                if (n.kind == Kind::Visit) return "I went to {player} about it, and we talked.";
                if (n.kind == Kind::Free) return "{player} found me where I was held and set me free.";
                if (n.kind == Kind::Talk) return "{player} came back and told me how it ended, and I paid what I had promised.";
                return {};
            default: (void)f; return {};
        }
    }

    // Keeps the end of a growing text within `limit` (the newest lines matter most).
    inline void Append(std::string& text, const std::string& line, std::size_t limit = kMaxLong) {
        if (line.empty()) return;
        if (!text.empty()) text += ' ';
        text += line;
        if (text.size() > limit) {
            auto cut = text.size() - limit;
            const auto space = text.find(' ', cut);
            text.erase(0, space == std::string::npos ? cut : space + 1);
        }
    }

    // The reward changes hands in person (the user's test game, 2026-11-19: "Faendal gave you 60 gold" with Faendal
    // four thousand units away, the tale having ended in a talk with Sven). A tale whose paying ending is reached
    // away from its asker gets one step more, written by the compiler (Tales.h): back to the asker, who hears how it
    // ended and pays. It is a talk with the asker that prepares no outcomes; the author's own talks always have some.
    constexpr float kWayBackHours = 72.0f;   // never gone back: the story ends as it had, unpaid
    inline bool IsWayBack(const Arc& a, const Node& n) {
        return a.pattern == Pattern::Tale && n.kind == Kind::Talk && n.who == RAsker && n.turns[0].empty();
    }

    inline Change Advance(Arc& a, const Facts& facts, float now) {
        Change c;
        if (a.phase != Phase::Open) return c;
        const auto nodes = NodesOf(a);
        if (a.node >= nodes.size()) return c;
        Facts f = facts;
        f.hoursHere = std::max(0.0f, (now - a.since) * 24.0f);
        auto end = [&](Ending e) {
            a.phase = Phase::Ended;
            a.ending = e;
            a.since = now;
            c.ending = e;
        };
        const Node& n = nodes[a.node];
        // On the way back for the reward the story has already ended as it did: the asker's death only costs the pay
        // (the step's own edge).
        if (f.dead[RAsker] && !IsWayBack(a, n)) {
            end(Ending::AskerDied);
            return c;
        }
        for (const auto& e : n.edges) {
            if (e.when == If{} || !EdgeHolds(a, n, e, f)) continue;
            a.visited |= 1u << a.node;
            a.flags |= e.setFlags;
            // A branch back to the same step (the rival would not be talked round) leaves it neither done nor
            // skipped: the player is still on it.
            const bool stays = e.next == a.node && e.ending == Ending::None;
            // A step left because someone it needed died is not done: the journal fails it with the tale.
            if (e.skip) c.skipped = a.node;
            else if (!stays && e.ending != Ending::Lost) c.done.push_back(a.node);
            c.stayed = stays;
            c.effect = e.effect;
            c.effectProp = e.effect == Effect::GiveItem || e.effect == Effect::Remedy ? e.prop : 0;
            c.effectRole = e.effectRole;
            c.log = e.log ? e.log : n.logStage;
            c.effect2 = e.effect2;
            c.effect2Role = e.effect2Role;
            c.bondWith = e.bondWith;
            if (!e.line.empty()) {
                // The tale's journal (the player's own words).
                c.line = e.line;
                Append(a.story, e.line);
            }
            if (a.pattern == Pattern::Tale && n.who != RNone) {
                // What the person of this step saw of it, in words true for them (never the player's journal).
                Append(a.heard[n.who], Witnessed(n, e, f), kMaxLong / 2);
            }
            a.deadline += n.extendDays;
            if (n.kind == Kind::Choose) a.accused = AnswerRole(a, n, f.answer);
            a.since = now;
            if (e.ending != Ending::None) {
                end(e.ending);
            } else if (e.next != kNoNode && e.next < nodes.size()) {
                a.node = e.next;
                // The way back keeps its own days, whatever was left of the tale's.
                if (IsWayBack(a, nodes[a.node])) a.deadline = std::max(a.deadline, now + (kWayBackHours + 24.0f) / 24.0f);
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

    // A quiet stretch after every story that came to the player or came to its end, whoever wants to bring the next
    // one and why (the user, 2026-11-19: Sven stood before the player with a new trouble moments after Faendal had
    // been turned down: "The pacing isn't quite right"). An offer that was already owed (its asker missed the player,
    // or is on the road from another hold) only waits out the shorter one.
    constexpr float kQuietHours = 6.0f;
    constexpr float kQuietHoursOwed = 1.0f;
    inline bool QuietFor(float lastBeat, float now, float hours) {
        return lastBeat < 0.0f || now < lastBeat || (now - lastBeat) * 24.0f >= hours;
    }

    class Book {
    public:
        std::vector<Arc> all;
        std::uint32_t nextId = 1;
        Pattern lastPattern = Pattern::None;
        std::uint8_t lastSlot = 0xFF;

        /** When a story last came to the player or ended (game days; -1 = never): the offers made and answered and
            the endings, never a plan dropped unseen. `skip`: an arc left out (the one that asks). */
        float LastBeat(std::uint32_t skip = 0) const {
            float last = -1.0f;
            for (const auto& a : all) {
                if (a.id == skip || a.ending == Ending::Lapsed) continue;
                last = std::max(last, a.created);
                if (a.phase != Phase::Planned) last = std::max(last, a.opened);
                if (a.phase == Phase::Ended) last = std::max(last, a.since);
            }
            return last;
        }

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
            // A tale is a new story every time (the ledger keeps its genres apart); a pattern never runs twice in a row.
            if (a.pattern == lastPattern && a.pattern != Pattern::Tale) return "the same kind of story ran last";
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
            a->deadline = now + (a->days > 0.0f ? a->days : Def(a->pattern).days);
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

    // ---- a tale's words (its author's, kept in the record)

    inline std::string TaleRoleLines(const Arc& a, Role r) {
        const bool open = a.phase == Phase::Open;
        const bool planned = a.phase == Phase::Planned;
        if (!open && !planned) return {};
        std::string out = a.truth[r];
        if (r == RAsker) {
            if (planned) {
                Append(out, "I am going to ask {player} for help: " + a.offerAsk + " I will pay {gold} gold for it.");
                return out;
            }
            Append(out, "I asked {player} for help: " + a.offerAsk + " For {gold} gold, paid once it is done, not before. It is not done yet.");
            Append(out, a.paid ? "I have already paid {player} {paid} gold of it." : "I have paid nothing yet.");
        }
        if (open && !a.heard[r].empty()) Append(out, "What has happened with it so far, as I saw it: " + a.heard[r]);
        return out;
    }

    inline std::string TaleEndMemory(const Arc& a, Role r) {
        const int k = TaleEndIndex(a.ending);
        if (k >= 0) {
            const auto& m = a.ends[static_cast<std::size_t>(k)].memory[r];
            if (!m.empty()) return m;
        }
        if (r != RAsker) return {};
        if (a.ending == Ending::Ignored) return "I asked {player} for help: " + a.offerAsk + " {player} never saw it through.";
        if (a.ending == Ending::Declined) return "I asked {player} for help: " + a.offerAsk + " {player} refused.";
        if (a.ending == Ending::Lost) return "I asked {player} for help: " + a.offerAsk + " It came to nothing in the end: someone it hung on died.";
        return {};
    }

    inline std::string TaleRumor(const Arc& a) {
        if (a.phase != Phase::Ended) return {};
        const int k = TaleEndIndex(a.ending);
        return k >= 0 ? a.ends[static_cast<std::size_t>(k)].rumor : std::string();
    }

    // The journal's closing line of a tale that ended.
    inline std::string TaleClosing(const Arc& a) {
        const int k = TaleEndIndex(a.ending);
        if (k >= 0 && !a.ends[static_cast<std::size_t>(k)].journal.empty()) return a.ends[static_cast<std::size_t>(k)].journal;
        if (a.ending == Ending::AskerDied) return "{asker} is dead.";
        if (a.ending == Ending::Ignored) return "I never saw it through, and {asker} has stopped waiting.";
        if (a.ending == Ending::Lost) return "There is nothing more to be done about it now.";
        return "It is over.";
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
        if (a.pattern == Pattern::Tale) return a.offerWords;
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
    // A story that began as talk in town was written with the player walking up to its asker ("how they receive a
    // stranger who has heard"). When it is the asker who comes to the player (they met by chance, or the asker set
    // out), that would be said the wrong way round ("You've been asking around about Torvald", played 2026-11-20):
    // what brings them is then said from the facts, with their own question.
    inline std::string OfferNarrationComing(const Arc& a) {
        return "{asker} has had no help from the talk in town and has been asking around. Now {asker} comes up to {player}, whom {asker} does "
               "not know, and asks in person, wary and hopeful: \"" + a.offerAsk + "\" It is {asker} who came looking for help; {player} has "
               "asked nothing yet";
    }
    inline std::string OfferPrompt(const Arc& a) {
        if (a.pattern == Pattern::Tale) return a.gold ? a.offerAsk + " Reward: {gold} gold." : a.offerAsk;
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
        if (a.pattern == Pattern::Tale) return TaleRoleLines(a, r);
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
                    if (a.flags & Flag::Fists) {
                        out += " {player} means to settle it with fists against {rival}, fighting for me; I do not fight {rival} myself. "
                               "It is not done yet.";
                    }
                    if (a.flags & Flag::Peace) out += " {player} is trying to talk {rival} into making peace; it is not done yet.";
                    if (a.flags & Flag::Refused) out += " {rival} would not be talked round so far.";
                    return out;
                }
                if (r == RRival) {
                    if (!open && !planned) return {};
                    out = std::string(FeudGrievance(a.seed).rival) + " {asker} and I are at odds, and I will not be the one to back down.";
                    if (open) out += " {player} has taken it on, for {asker}. If {player} asks for my side of it, I give it.";
                    if (a.flags & Flag::Fists) {
                        out += " {player} wants to settle it with fists, fighting me for {asker}; I will not back down from it. "
                               "The fight is between {player} and me; {asker} does not fight me.";
                    }
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
        if (a.pattern == Pattern::Tale) return TaleEndMemory(a, r);
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
                    if (a.ending == Ending::WrongAccused) {
                        return "{player} named {accused} as {victim}'s killer, but nothing {player} showed me points to {accused}. I do not "
                               "believe it, and whoever killed {victim} is still free.";
                    }
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
        if (a.pattern == Pattern::Tale) return TaleRumor(a);
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
                if (a.ending == Ending::Declined || a.ending == Ending::Lapsed) return "{victim} was found dead at home, killed by someone nobody has named.";
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
    constexpr std::uint32_t kRecordVersion = 11;   // 11: the words written in a paper; 10: a trail of three things to examine, what an ending leaves open, sequels; 2: tales; 3: a visit's why; 4: places out in the world; 5: ambushes; 6: stalkers; 7: someone held at a place; 8: remains to examine; 9: what walks by night. Older ones load

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
        // At most `limit` bytes, never cutting a UTF-8 character in two.
        inline std::size_t Fit(const std::string& text, std::size_t limit) {
            if (text.size() <= limit) return text.size();
            std::size_t n = limit;
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
            return n;
        }
        inline void PutText(std::vector<std::uint8_t>& out, const std::string& text, std::size_t limit = kMaxTextBytes) {
            const auto size = static_cast<std::uint16_t>(Fit(text, limit));
            Put(out, size);
            out.insert(out.end(), text.begin(), text.begin() + size);
        }
        inline bool GetText(std::span<const std::uint8_t> in, std::size_t& at, std::string& text, std::size_t limit = kMaxTextBytes) {
            std::uint16_t size = 0;
            if (!Get(in, at, size) || size > limit || in.size() - at < size) return false;
            text.assign(reinterpret_cast<const char*>(in.data() + at), size);
            at += size;
            return true;
        }
        inline void PutLong(std::vector<std::uint8_t>& out, const std::string& text) { PutText(out, text, kMaxLongBytes); }
        inline bool GetLong(std::span<const std::uint8_t> in, std::size_t& at, std::string& text) { return GetText(in, at, text, kMaxLongBytes); }

        inline void PutNode(std::vector<std::uint8_t>& out, const Node& n) {
            Put(out, static_cast<std::uint8_t>(n.kind));
            Put(out, static_cast<std::uint8_t>(n.who));
            Put(out, n.prop);
            Put(out, n.objective);
            Put(out, static_cast<std::uint8_t>(n.marker));
            Put(out, n.logStage);
            Put(out, n.extendDays);
            for (const auto& o : n.options) PutText(out, o);
            for (const auto r : n.optionRoles) Put(out, static_cast<std::uint8_t>(r));
            PutText(out, n.goal);
            PutText(out, n.why);
            PutText(out, n.question);
            for (const auto& turn : n.turns) PutText(out, turn);
            Put(out, n.waitHours);
            Put(out, n.site);
            std::uint8_t edges = 0;
            for (const auto& e : n.edges) edges = static_cast<std::uint8_t>(edges + (e.when != If{} ? 1 : 0));
            Put(out, edges);
            for (const auto& e : n.edges) {
                if (e.when == If{}) continue;
                Put(out, static_cast<std::uint8_t>(e.when));
                Put(out, static_cast<std::uint8_t>(e.role));
                Put(out, e.prop);
                Put(out, e.needProps);
                Put(out, e.next);
                Put(out, static_cast<std::uint8_t>(e.ending));
                Put(out, static_cast<std::uint8_t>(e.effect));
                Put(out, static_cast<std::uint8_t>(e.effectRole));
                Put(out, e.setFlags);
                Put(out, e.log);
                Put(out, static_cast<std::uint8_t>(e.skip ? 1 : 0));
                Put(out, static_cast<std::uint8_t>(e.effect2));
                Put(out, static_cast<std::uint8_t>(e.effect2Role));
                PutText(out, e.line);
                Put(out, e.hours);
                Put(out, static_cast<std::uint8_t>(e.bondWith));
            }
        }

        inline bool ValidEnding(std::uint8_t v) { return v == 0 || (v >= 10 && v <= 20); }

        inline bool GetNode(std::span<const std::uint8_t> in, std::size_t& at, Node& n, std::uint32_t version) {
            std::uint8_t kind = 0, who = 0, marker = 0, edges = 0;
            if (!Get(in, at, kind) || !Get(in, at, who) || !Get(in, at, n.prop) || !Get(in, at, n.objective) || !Get(in, at, marker) ||
                !Get(in, at, n.logStage) || !Get(in, at, n.extendDays)) {
                return false;
            }
            const auto lastKind = static_cast<std::uint8_t>(version >= 9 ? Kind::Haunt : version >= 8 ? Kind::Examine : version >= 7 ? Kind::Free : version >= 6 ? Kind::Stalk : version >= 5 ? Kind::Ambush : Kind::Slay);
            if (kind < 1 || kind > lastKind || who >= kRoles || marker >= kRoles || n.prop >= kProps) return false;
            n.kind = static_cast<Kind>(kind);
            n.who = static_cast<Role>(who);
            n.marker = static_cast<Role>(marker);
            for (auto& o : n.options) if (!GetText(in, at, o)) return false;
            for (auto& r : n.optionRoles) {
                std::uint8_t v = 0;
                if (!Get(in, at, v) || v >= kRoles) return false;
                r = static_cast<Role>(v);
            }
            if (!GetText(in, at, n.goal) || (version >= 3 && !GetText(in, at, n.why)) || !GetText(in, at, n.question)) return false;
            for (auto& turn : n.turns) if (!GetText(in, at, turn)) return false;
            if (!Get(in, at, n.waitHours) || !std::isfinite(n.waitHours)) return false;
            if (version >= 4 && (!Get(in, at, n.site) || n.site > kSites)) return false;
            if (!Get(in, at, edges) || edges > n.edges.size()) return false;
            for (std::uint8_t i = 0; i < edges; ++i) {
                auto& e = n.edges[i];
                std::uint8_t when = 0, role = 0, ending = 0, effect = 0, effectRole = 0, skip = 0, effect2 = 0, effect2Role = 0, bondWith = 0;
                if (!Get(in, at, when) || !Get(in, at, role) || !Get(in, at, e.prop) || !Get(in, at, e.needProps) || !Get(in, at, e.next) ||
                    !Get(in, at, ending) || !Get(in, at, effect) || !Get(in, at, effectRole) || !Get(in, at, e.setFlags) ||
                    !Get(in, at, e.log) || !Get(in, at, skip) || !Get(in, at, effect2) || !Get(in, at, effect2Role) ||
                    !GetText(in, at, e.line) || !Get(in, at, e.hours) || !Get(in, at, bondWith)) {
                    return false;
                }
                if (when < 1 || when > static_cast<std::uint8_t>(version >= 9 ? If::Rested : If::Freed) || role >= kRoles || effectRole >= kRoles ||
                    effect2Role >= kRoles || bondWith >= kRoles || effect > kEffects || effect2 > kEffects || !ValidEnding(ending) ||
                    e.prop >= kProps || !std::isfinite(e.hours)) {
                    return false;
                }
                e.when = static_cast<If>(when);
                e.role = static_cast<Role>(role);
                e.ending = static_cast<Ending>(ending);
                e.effect = static_cast<Effect>(effect);
                e.effectRole = static_cast<Role>(effectRole);
                e.skip = skip != 0;
                e.effect2 = static_cast<Effect>(effect2);
                e.effect2Role = static_cast<Role>(effect2Role);
                e.bondWith = static_cast<Role>(bondWith);
            }
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
            // ---- version 2: a tale's own
            const auto nodes = static_cast<std::uint8_t>(std::min(a.graph.size(), kMaxTaleNodes));
            detail::Put(out, nodes);
            for (std::uint8_t n = 0; n < nodes; ++n) detail::PutNode(out, a.graph[n]);
            detail::PutText(out, a.genre);
            detail::PutLong(out, a.premise);
            detail::PutLong(out, a.offerWords);
            detail::PutText(out, a.offerAsk);
            detail::PutLong(out, a.opening);
            for (std::size_t r = 0; r < kRoles; ++r) {
                detail::PutText(out, a.roleName[r]);
                detail::PutLong(out, a.truth[r]);
                detail::PutLong(out, a.heard[r]);
            }
            for (std::size_t p = 0; p < kProps; ++p) {
                detail::Put(out, a.propHome[p]);
                detail::Put(out, a.propPocket[p]);
            }
            for (const auto& end : a.ends) {
                detail::PutLong(out, end.journal);
                detail::PutLong(out, end.rumor);
                for (const auto& m : end.memory) detail::PutLong(out, m);
                detail::Put(out, static_cast<std::uint8_t>(end.pay ? 1 : 0));
            }
            detail::PutLong(out, a.story);
            detail::Put(out, a.price);
            detail::Put(out, a.days);
            // ---- version 4: places out in the world
            for (std::size_t s = 0; s < kSites; ++s) {
                detail::Put(out, a.site[s]);
                detail::Put(out, a.siteAnchor[s]);
                detail::Put(out, a.siteBoss[s]);
                detail::Put(out, a.siteKind[s]);
                detail::PutText(out, a.bossName[s]);
                detail::Put(out, a.siteState[s]);
            }
            for (const auto p : a.propSite) detail::Put(out, p);
            // ---- version 5: an ambush
            for (const auto t : a.thug) detail::Put(out, t);
            detail::PutText(out, a.thugName);
            detail::Put(out, a.propAmbush);
            // ---- version 7: someone held at a place
            detail::Put(out, a.captive);
            detail::Put(out, a.captiveSite);
            detail::Put(out, a.captiveState);
            // ---- version 8: remains to examine (the two a record of that version holds; the third is below)
            for (std::size_t k = 0; k < 2; ++k) {
                detail::Put(out, a.clueWhat[k]);
                detail::Put(out, a.clueSite[k]);
                detail::Put(out, a.clueRef[k]);
                detail::Put(out, a.clueState[k]);
            }
            // ---- version 9: what walks by night
            detail::Put(out, a.spiritSite);
            detail::Put(out, a.spiritWoman);
            detail::Put(out, a.spiritWants);
            detail::Put(out, a.spiritState);
            detail::Put(out, a.spiritRef);
            detail::PutText(out, a.spiritName);
            detail::PutLong(out, a.spiritTruth);
            // ---- version 10: the third thing to examine, how far out each lies, what each ending leaves open, sequels
            detail::Put(out, a.clueWhat[2]);
            detail::Put(out, a.clueSite[2]);
            detail::Put(out, a.clueRef[2]);
            detail::Put(out, a.clueState[2]);
            for (const auto band : a.clueFar) detail::Put(out, band);
            for (const auto& e : a.ends) detail::PutLong(out, e.after);
            detail::Put(out, a.sequelOf);
            detail::Put(out, a.followed);
            // ---- version 11: the words written in a paper (2026-11-24: before, a story had no papers to turn on)
            for (const auto& words : a.propText) detail::PutLong(out, words);
        }
        return out;
    }

    // All or nothing. Version 1 records (before tales) load as they were.
    inline bool Deserialize(std::span<const std::uint8_t> in, Book& book, std::uint32_t version = kRecordVersion) {
        if (version < 1 || version > kRecordVersion) return false;
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
            if (version >= 2) {
                std::uint8_t nodes = 0;
                if (!detail::Get(in, at, nodes) || nodes > kMaxTaleNodes) return false;
                a.graph.resize(nodes);
                for (auto& n : a.graph) if (!detail::GetNode(in, at, n, version)) return false;
                if (!detail::GetText(in, at, a.genre) || !detail::GetLong(in, at, a.premise) || !detail::GetLong(in, at, a.offerWords) ||
                    !detail::GetText(in, at, a.offerAsk) || !detail::GetLong(in, at, a.opening)) {
                    return false;
                }
                for (std::size_t r = 0; r < kRoles; ++r) {
                    if (!detail::GetText(in, at, a.roleName[r]) || !detail::GetLong(in, at, a.truth[r]) || !detail::GetLong(in, at, a.heard[r])) return false;
                }
                for (std::size_t p = 0; p < kProps; ++p) {
                    if (!detail::Get(in, at, a.propHome[p]) || !detail::Get(in, at, a.propPocket[p]) || a.propHome[p] >= kRoles ||
                        a.propPocket[p] >= kRoles) {
                        return false;
                    }
                }
                for (auto& end : a.ends) {
                    std::uint8_t pay = 0;
                    if (!detail::GetLong(in, at, end.journal) || !detail::GetLong(in, at, end.rumor)) return false;
                    for (auto& m : end.memory) if (!detail::GetLong(in, at, m)) return false;
                    if (!detail::Get(in, at, pay)) return false;
                    end.pay = pay != 0;
                }
                if (!detail::GetLong(in, at, a.story) || !detail::Get(in, at, a.price) || !detail::Get(in, at, a.days) || !std::isfinite(a.days)) {
                    return false;
                }
            }
            if (version >= 4) {
                for (std::size_t s = 0; s < kSites; ++s) {
                    if (!detail::Get(in, at, a.site[s]) || !detail::Get(in, at, a.siteAnchor[s]) || !detail::Get(in, at, a.siteBoss[s]) ||
                        !detail::Get(in, at, a.siteKind[s]) || !detail::GetText(in, at, a.bossName[s]) || !detail::Get(in, at, a.siteState[s])) {
                        return false;
                    }
                }
                for (auto& p : a.propSite) {
                    if (!detail::Get(in, at, p) || p > kSites) return false;
                }
            }
            if (version >= 5) {
                for (auto& t : a.thug) if (!detail::Get(in, at, t)) return false;
                if (!detail::GetText(in, at, a.thugName) || !detail::Get(in, at, a.propAmbush) || a.propAmbush >= (1u << kProps)) return false;
            }
            if (version >= 7) {
                if (!detail::Get(in, at, a.captive) || !detail::Get(in, at, a.captiveSite) || !detail::Get(in, at, a.captiveState) ||
                    a.captive >= kRoles || a.captiveSite > kSites || a.captiveState > CaptiveHome || (a.captive != 0) != (a.captiveSite != 0)) {
                    return false;
                }
            }
            if (version >= 8) {
                for (std::size_t k = 0; k < 2; ++k) {
                    if (!detail::Get(in, at, a.clueWhat[k]) || !detail::Get(in, at, a.clueSite[k]) || !detail::Get(in, at, a.clueRef[k]) ||
                        !detail::Get(in, at, a.clueState[k]) || a.clueWhat[k] > kRemains || a.clueSite[k] > kSites || a.clueState[k] > ClueSeen ||
                        (a.clueWhat[k] != 0) != (a.clueSite[k] != 0)) {
                        return false;
                    }
                }
            }
            if (version >= 9) {
                if (!detail::Get(in, at, a.spiritSite) || !detail::Get(in, at, a.spiritWoman) || !detail::Get(in, at, a.spiritWants) ||
                    !detail::Get(in, at, a.spiritState) || !detail::Get(in, at, a.spiritRef) || !detail::GetText(in, at, a.spiritName) ||
                    !detail::GetLong(in, at, a.spiritTruth) || a.spiritSite > kSites || a.spiritWoman > 1 || a.spiritWants > kProps ||
                    a.spiritState > SpiritRested) {
                    return false;
                }
            }
            if (version >= 10) {
                if (!detail::Get(in, at, a.clueWhat[2]) || !detail::Get(in, at, a.clueSite[2]) || !detail::Get(in, at, a.clueRef[2]) ||
                    !detail::Get(in, at, a.clueState[2]) || a.clueWhat[2] > kRemains || a.clueSite[2] > kSites || a.clueState[2] > ClueSeen ||
                    (a.clueWhat[2] != 0) != (a.clueSite[2] != 0)) {
                    return false;
                }
                for (auto& band : a.clueFar) {
                    if (!detail::Get(in, at, band) || band > 2) return false;
                }
                for (auto& e : a.ends) {
                    if (!detail::GetLong(in, at, e.after)) return false;
                }
                if (!detail::Get(in, at, a.sequelOf) || !detail::Get(in, at, a.followed) || a.followed > 1) return false;
            }
            if (version >= 11) {
                for (auto& words : a.propText) {
                    if (!detail::GetLong(in, at, words)) return false;
                }
            }
            const bool tale = pattern == static_cast<std::uint8_t>(Pattern::Tale);
            const bool known = pattern >= 1 && pattern <= kPatterns;
            const std::size_t steps = tale ? a.graph.size() : Def(known ? static_cast<Pattern>(pattern) : Pattern::None).nodes.size();
            if (!known || phase > 2 || !detail::ValidEnding(ending) || accused >= kRoles || (a.slot != 0xFF && a.slot >= kSlots) ||
                a.node >= steps || (tale && a.graph.empty()) || !std::isfinite(a.deadline)) {
                return false;
            }
            for (const auto& n : a.graph) {
                for (const auto& e : n.edges) {
                    if (e.when != If{} && e.next != kNoNode && e.next >= a.graph.size()) return false;
                }
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
