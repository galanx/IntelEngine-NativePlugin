// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/ArcsCheck.cpp
#include "Arcs.h"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine::Arcs;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Arc Make(Pattern p, std::uint32_t first = 100) {
    Arc a;
    a.pattern = p;
    for (std::size_t r = 1; r < kRoles; ++r) a.cast[r] = first + static_cast<std::uint32_t>(r);
    a.gold = 200;
    a.seed = 7;
    a.order = ShuffleSuspects(a.seed);
    a.propBase[0] = 77;
    a.propName[0] = "Ysolda's Silver Necklace";
    return a;
}

static std::uint32_t Open(Book& book, Arc a, float now) {
    const auto id = book.Plan(std::move(a), now);
    Require(id != 0, "an arc could not be planned");
    Require(book.Accept(id, now), "an arc could not be accepted");
    return id;
}

static Facts With(Role r) {
    Facts f;
    f.with[r] = true;
    f.spoke[r] = true;
    return f;
}

static void ThiefInOrder() {
    Book book;
    const auto id = Open(book, Make(Pattern::Thief), 1.0f);
    Arc& a = *book.Find(id);
    Require(a.node == 0, "a thief arc starts at the witness");
    // Talking to someone else does nothing.
    Require(!Advance(a, With(RCulprit), 1.1f).any(), "talking to the culprit is not asking the witness");
    auto c = Advance(a, With(RWitness), 1.2f);
    Require(c.done.size() == 1 && c.done[0] == 0 && a.node == 1, "the witness heard moves to finding the thing");
    Require(c.log == 1010, "the witness heard writes the node's journal line");
    Require((a.flags & Flag::WitnessHeard) != 0, "the witness flag is set");
    Facts has;
    has.has[0] = true;
    c = Advance(a, has, 1.3f);
    Require(a.node == 2 && c.done[0] == 1, "the thing found moves to the confrontation");
    // No answer yet: nothing.
    Require(!Advance(a, has, 1.4f).any(), "no answer, no change");
    Facts answered = has;
    answered.answer = 0;
    c = Advance(a, answered, 1.5f);
    Require(a.node == 3 && c.effect == Effect::Jail && c.effectRole == RCulprit, "reporting the thief jails them");
    Require((a.flags & Flag::Reported) != 0, "the reported flag is set");
    Require(c.log == 1031, "handing the thief over writes its own journal line");
    Facts handed;
    handed.handed[0] = true;
    c = Advance(a, handed, 1.6f);
    Require(a.phase == Phase::Ended && a.ending == Ending::Done && c.effect == Effect::PayGold, "returned: done and paid");
}

static void ThiefShortcutsAndEnds() {
    Book book;
    // Found before the witness was asked: straight to the confrontation, one node a look.
    auto id = Open(book, Make(Pattern::Thief), 1.0f);
    Arc* a = book.Find(id);
    Facts has;
    has.has[0] = true;
    auto shortcut = Advance(*a, has, 1.1f);
    Require(a->node == 2, "found first: to the confrontation");
    Require(shortcut.done.empty() && shortcut.skipped == 0 && shortcut.log == 1020,
            "found first: the witness was never asked (objective taken away), the journal says it was found");
    // The thief dead at the confrontation: on to returning it.
    Facts dead = has;
    dead.dead[RCulprit] = true;
    Advance(*a, dead, 1.2f);
    Require(a->node == 3, "the thief dead: return it");
    // Kept past the deadline.
    Advance(*a, has, a->deadline + 0.01f);
    Require(a->phase == Phase::Ended && a->ending == Ending::Kept, "kept past the deadline");

    book = Book{};
    id = Open(book, Make(Pattern::Thief, 200), 1.0f);
    a = book.Find(id);
    Advance(*a, Facts{}, a->deadline + 0.01f);
    Require(a->ending == Ending::Ignored, "nothing done: ignored");

    book = Book{};
    id = Open(book, Make(Pattern::Thief, 300), 1.0f);
    a = book.Find(id);
    Facts askerDead;
    askerDead.dead[RAsker] = true;
    askerDead.has[0] = true;
    Advance(*a, askerDead, 1.1f);
    Require(a->ending == Ending::AskerDied, "the asker dead ends it first");

    // Let go instead of reported.
    book = Book{};
    id = Open(book, Make(Pattern::Thief, 400), 1.0f);
    a = book.Find(id);
    Advance(*a, has, 1.1f);
    Facts spare = has;
    spare.answer = 1;
    const auto c = Advance(*a, spare, 1.2f);
    Require(a->node == 3 && c.effect == Effect::None && (a->flags & Flag::Spared), "let go: no jail, spared");
    Require(c.log == 1032, "letting the thief go writes its own journal line");

    // Handed back before any confrontation: done, paid, and the thief got away with it.
    book = Book{};
    id = Open(book, Make(Pattern::Thief, 500), 1.0f);
    a = book.Find(id);
    Advance(*a, has, 1.1f);
    Require(a->node == 2, "found first again");
    Facts given = has;
    given.handed[0] = true;
    const auto back = Advance(*a, given, 1.2f);
    Require(a->ending == Ending::Done && back.effect == Effect::PayGold && back.effectRole == RAsker &&
                !(a->flags & (Flag::Reported | Flag::Spared)),
            "handed back before the confrontation: done and paid, the thief neither reported nor spared");
    Require(EndMemory(*a, RCulprit).find("Nobody has come for me") != std::string::npos, "the thief knows they got away with it");
}

static void Feud() {
    for (int branch = 0; branch < 5; ++branch) {
        Book book;
        const auto id = Open(book, Make(Pattern::Feud, 100 + static_cast<std::uint32_t>(branch) * 10), 1.0f);
        Arc& a = *book.Find(id);
        Require(!Advance(a, With(RAsker), 1.1f).any(), "the asker talking is not the rival's side");
        Advance(a, With(RRival), 1.2f);
        Require(a.node == 1 && (a.flags & Flag::HeardRival), "the rival heard: decide");
        Facts f;
        f.answer = branch == 0 || branch == 1 ? 0 : branch == 2 || branch == 3 ? 1 : 2;
        Advance(a, f, 1.3f);
        if (branch <= 1) {
            Require(a.node == 2 && (a.flags & Flag::Fists), "fists: the brawl");
            Facts b;
            b.brawl = branch == 0 ? 1 : -1;
            const auto c = Advance(a, b, 1.4f);
            Require(a.ending == (branch == 0 ? Ending::Done : Ending::Failed), "won: done; lost: failed");
            Require((c.effect == Effect::PayGold) == (branch == 0), "paid only when won");
        } else if (branch <= 3) {
            Require(a.node == 3 && (a.flags & Flag::Peace), "peace: the talk with the rival");
            Facts p;
            p.answer = branch == 2 ? 0 : 1;
            if (branch == 2) {
                // Talking round needs the Speech for it: without, the rival refuses and the step stays.
                Facts weak = p;
                weak.speech = kPersuadeSpeech - 1;
                const auto no = Advance(a, weak, 1.35f);
                Require(a.phase == Phase::Open && a.node == 3 && (a.flags & Flag::Refused) && no.log == 2023,
                        "not persuasive enough: refused, still to make peace");
                p.speech = kPersuadeSpeech;
            }
            const auto c = Advance(a, p, 1.4f);
            Require(a.ending == Ending::Settled, "peace made");
            Require((c.effect == Effect::PlayerPays) == (branch == 3), "bought off: the player pays");
            Require((branch == 2 ? c.effect : c.effect2) == Effect::PayGold, "peace made: the asker pays the reward");
        } else {
            Require(a.ending == Ending::StayedOut, "stayed out");
        }
    }
}

static void Murder() {
    // Every order the suspects can be shown in, and each answer.
    std::set<int> culpritFirst;
    for (std::uint32_t seed = 0; seed < 64; ++seed) {
        const auto order = ShuffleSuspects(seed);
        std::set<int> roles{order[0], order[1], order[2]};
        Require(roles.size() == 3 && roles.count(RCulprit) && roles.count(RSuspect2) && roles.count(RSuspect3), "the order is a permutation");
        for (int i = 0; i < 3; ++i) if (order[static_cast<std::size_t>(i)] == RCulprit) culpritFirst.insert(i);
    }
    Require(culpritFirst.size() == 3, "the culprit is shown in every place over the seeds");

    for (int shown = 0; shown < 3; ++shown) {
        for (const bool proof : {true, false}) {
            Book book;
            const auto id = Open(book, Make(Pattern::Murder, 100 + static_cast<std::uint32_t>(shown) * 10), 1.0f);
            Arc& a = *book.Find(id);
            Facts scene;
            scene.atScene = true;
            Advance(a, scene, 1.1f);
            Require(a.node == 1 && (a.flags & Flag::SawScene), "the scene seen: the witness");
            Advance(a, With(RWitness), 1.2f);
            Require(a.node == 2, "the witness heard: find proof");
            Facts weapon;
            weapon.has[0] = true;
            Advance(a, weapon, 1.3f);
            Require(a.node == 3, "proof found: name the killer");
            Facts accuse;
            accuse.has[0] = proof;
            accuse.answer = shown;
            const Role named = a.order[static_cast<std::size_t>(shown)];
            const auto c = Advance(a, accuse, 1.4f);
            if (named == RCulprit && proof) {
                Require(a.ending == Ending::Done && c.effect == Effect::Jail && c.effectRole == RCulprit, "the killer named with proof");
            } else if (named == RCulprit) {
                Require(a.phase == Phase::Open && a.node == 2 && (a.flags & Flag::NoProof), "no proof: find it");
                Require(c.log == 3031, "no proof: the journal says proof is wanted");
            } else {
                Require(a.ending == Ending::WrongAccused && c.effect == Effect::Shame && c.effectRole == named, "an innocent named");
                Require(a.accused == named, "who was accused is kept");
            }
        }
    }
    // The letter alone is proof too, and found before the scene it goes straight to the naming.
    Book book;
    const auto id = Open(book, Make(Pattern::Murder), 1.0f);
    Arc& a = *book.Find(id);
    Facts letter;
    letter.has[1] = true;
    Advance(a, letter, 1.1f);
    Require(a.node == 3, "the letter found first: name the killer");
}

static void BookRules() {
    Book book;
    const auto first = book.Plan(Make(Pattern::Thief, 100), 1.0f);
    Require(first != 0, "planned");
    Require(book.Plan(Make(Pattern::Feud, 200), 1.0f) == 0, "one planned arc at a time");
    Require(book.Accept(first, 1.0f), "accepted");
    Require(book.Plan(Make(Pattern::Thief, 300), 1.1f) == 0, "the same kind of story not twice running");
    Arc shared = Make(Pattern::Feud, 400);
    shared.cast[RRival] = book.Find(first)->cast[RAsker];
    Require(book.Plan(shared, 1.1f) == 0, "nobody in two arcs at once");
    const auto second = book.Plan(Make(Pattern::Feud, 500), 1.1f);
    Require(second != 0 && book.Accept(second, 1.1f), "a second arc");
    Require(book.Plan(Make(Pattern::Murder, 600), 1.2f) == 0, "two open at most");
    Require(book.Find(first)->slot != book.Find(second)->slot, "different journal slots");
    // A planned arc whose offer never came lapses.
    Book lapse;
    const auto id = lapse.Plan(Make(Pattern::Murder, 700), 1.0f);
    Require(lapse.Lapse(1.5f).empty() && lapse.Lapse(2.1f).size() == 1 && lapse.Find(id)->ending == Ending::Lapsed, "lapsed");
    Require(EndStage(Pattern::Thief, Ending::Declined) == 0 && EndStage(Pattern::Murder, Ending::Done) == 3090, "end stages");
}

static void SaveData() {
    Book book;
    const auto id = Open(book, Make(Pattern::Murder), 1.0f);
    Arc& a = *book.Find(id);
    a.flags = Flag::SawScene | Flag::WitnessHeard;
    a.node = 2;
    a.propAt[0] = 0xFF000801;
    a.title = "Murder in Riverwood";
    a.paid = 40;
    a.rumor = 77;
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "round trip");
    const Arc& b = *back.Find(id);
    Require(b.node == 2 && b.flags == a.flags && b.cast == a.cast && b.order == a.order && b.propName[0] == a.propName[0] &&
                b.propAt[0] == a.propAt[0] && b.title == a.title && b.deadline == a.deadline && b.paid == 40 && b.rumor == 77,
            "every field comes back");
    auto bad = bytes;
    bad.pop_back();
    Book untouched = back;
    Require(!Deserialize(bad, untouched), "a short record is refused");
    Require(untouched.all.size() == back.all.size(), "a refused record leaves the book as it was");
    Require(!Deserialize(bytes, untouched, 2), "an unknown version is refused");
}

static void Words() {
    Arc a = Make(Pattern::Murder);
    a.phase = Phase::Open;
    for (std::size_t r = 1; r < kRoles; ++r) {
        const auto line = RoleLines(a, static_cast<Role>(r));
        if (r == RRival || r == RVictim) Require(line.empty(), "a murder has no rival, and the dead say nothing");
        else Require(!line.empty(), "every role of a murder knows something");
    }
    Require(RoleLines(a, RAsker).find("I have paid nothing") != std::string::npos, "the asker says what is not yet true");
    Require(RoleLines(a, RCulprit).find("never admit") != std::string::npos, "the killer lies");
    a.phase = Phase::Ended;
    a.ending = Ending::WrongAccused;
    a.accused = RSuspect2;
    Require(!EndMemory(a, RSuspect2).empty() && EndMemory(a, RSuspect3).empty(), "only the wrongly accused remembers being named");
    Require(Rumor(a).find("{accused}") != std::string::npos, "the town hears who was accused");
}

int main() {
    try {
        ThiefInOrder();
        ThiefShortcutsAndEnds();
        Feud();
        Murder();
        BookRules();
        SaveData();
        Words();
        std::cout << "ArcsCheck: all passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ArcsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
}
