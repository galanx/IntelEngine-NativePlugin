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
                Require(c.effect2 == Effect::PayGold && c.effect2Role == RAsker, "the killer named: the asker pays the reward");
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
    // The quiet stretch: counted from the last offer made or answered or the last ending, never from a plan that
    // was dropped unseen.
    Book quiet;
    Require(quiet.LastBeat() < 0.0f && QuietFor(quiet.LastBeat(), 5.0f, kQuietHours), "nothing told yet: no wait");
    const auto told = quiet.Plan(Make(Pattern::Thief, 100), 2.0f);
    Require(quiet.LastBeat() == 2.0f && !QuietFor(quiet.LastBeat(), 2.0f + 5.0f / 24.0f, kQuietHours), "five hours after an offer: still quiet");
    Require(QuietFor(quiet.LastBeat(), 2.0f + 6.5f / 24.0f, kQuietHours), "six and a half hours after: free");
    Require(quiet.EndPlanned(told, Ending::Declined, 2.5f) && quiet.LastBeat() == 2.5f, "a refusal counts from when it was said");
    Require(quiet.LastBeat(told) < 0.0f, "the asking arc itself is left out");
    const auto unseen = quiet.Plan(Make(Pattern::Feud, 200), 4.0f);
    Require(quiet.EndPlanned(unseen, Ending::Lapsed, 4.1f) && quiet.LastBeat() == 2.5f, "a plan dropped unseen is no beat");
    Require(QuietFor(3.0f, 2.0f, kQuietHours), "a clock that went back never blocks");
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
    Require(!Deserialize(bytes, untouched, kRecordVersion + 1), "an unknown version is refused");
    // ---- talk in town, and the one it is about: who asks around, who sets out, and when
    {
        Arc plain;
        plain.seed = 12345u;
        Require(!AsksAround(plain) && !SetsOut(plain) && !MaySetOutNow(plain, 10.0f, 12.0f), "a story that did not begin as talk: nobody asks around, nobody sets out");
        int ask = 0, out = 0;
        for (std::uint32_t seed = 1; seed <= 2000; ++seed) {
            Arc talk;
            talk.flags = Flag::Rumored;
            talk.seed = seed * 2654435761u;
            ask += AsksAround(talk) ? 1 : 0;
            out += SetsOut(talk) ? 1 : 0;
            Require(AsksAround(talk) == AsksAround(talk) && SetsOut(talk) == SetsOut(talk), "fixed by the story's own seed");
        }
        Require(ask > 800 && ask < 1200, "about half of them stop someone on the way");
        Require(out > 500 && out < 860, "about one in three sets out: it stays the rarer way");
        Arc goer;
        goer.flags = Flag::Rumored;
        for (goer.seed = 1; !SetsOut(goer); ++goer.seed) {}
        goer.created = 10.0f;
        Require(!MaySetOutNow(goer, 10.0f + 5.9f / 24.0f, 12.0f), "not before the talk has gone round for a good part of a day");
        Require(MaySetOutNow(goer, 10.0f + 6.1f / 24.0f, 12.0f), "then, by day");
        Require(!MaySetOutNow(goer, 10.5f, 21.0f) && !MaySetOutNow(goer, 10.5f, 3.0f) && MaySetOutNow(goer, 10.5f, 8.0f), "never by night");
        Require(!MaySetOutNow(goer, 9.5f, 12.0f), "not on a clock set back");
        goer.phase = Phase::Open;
        Require(!MaySetOutNow(goer, 10.5f, 12.0f), "not once the story is taken on");
        Arc stayer;
        stayer.flags = Flag::Rumored;
        for (stayer.seed = 1; SetsOut(stayer); ++stayer.seed) {}
        stayer.created = 10.0f;
        Require(!MaySetOutNow(stayer, 12.0f, 12.0f), "the others wait as talk until the player passes by");
    }

    // A save made before tales (record version 1: no tale block) still loads, every pattern arc as it was.
    std::vector<std::uint8_t> v1;
    detail::Put(v1, book.nextId);
    detail::Put(v1, static_cast<std::uint8_t>(book.lastPattern));
    detail::Put(v1, book.lastSlot);
    detail::Put(v1, static_cast<std::uint32_t>(book.all.size()));
    for (const auto& x : book.all) {
        detail::Put(v1, x.id);
        detail::Put(v1, static_cast<std::uint8_t>(x.pattern));
        detail::Put(v1, static_cast<std::uint8_t>(x.phase));
        detail::Put(v1, static_cast<std::uint8_t>(x.ending));
        detail::Put(v1, x.slot);
        detail::Put(v1, x.node);
        detail::Put(v1, x.visited);
        detail::Put(v1, x.flags);
        for (const auto who : x.cast) detail::Put(v1, who);
        for (std::size_t p = 0; p < kProps; ++p) {
            detail::Put(v1, x.propBase[p]);
            detail::Put(v1, x.propAt[p]);
            detail::PutText(v1, x.propName[p]);
        }
        for (const auto r : x.order) detail::Put(v1, static_cast<std::uint8_t>(r));
        detail::Put(v1, x.place);
        detail::Put(v1, x.scene);
        detail::Put(v1, x.gold);
        detail::Put(v1, x.paid);
        detail::Put(v1, x.rumor);
        detail::Put(v1, x.seed);
        detail::Put(v1, static_cast<std::uint8_t>(x.accused));
        detail::Put(v1, x.created);
        detail::Put(v1, x.opened);
        detail::Put(v1, x.since);
        detail::Put(v1, x.deadline);
        detail::PutText(v1, x.title);
    }
    Book old;
    Require(Deserialize(v1, old, 1), "a record of version 1 loads");
    const Arc& o = *old.Find(id);
    Require(o.node == 2 && o.cast == a.cast && o.title == a.title && o.paid == 40 && o.graph.empty(), "and its arc is whole");
    Require(!Deserialize(v1, untouched, 4), "a version 1 record read as version 4 is refused, not misread");
}

static void WhoGoes() {
    // An innkeeper the player has never spoken with, the player out at a farm of her town (played 2026-11-21).
    Going innkeeper;
    innkeeper.post = true;
    Require(StaysPut(innkeeper) == Stays::Post, "an innkeeper does not leave her inn to look for anyone");
    innkeeper.inTown = true;
    innkeeper.knows = 2;
    Require(StaysPut(innkeeper) == Stays::Post, "not even for someone she knows, across town");
    innkeeper.with = true;
    Require(StaysPut(innkeeper) == Stays::No, "but she speaks up when the player stands at her counter");
    Going stranger;
    stranger.inTown = true;
    Require(StaysPut(stranger) == Stays::Stranger, "a stranger does not go looking for the player");
    stranger.askingAround = true;
    Require(StaysPut(stranger) == Stays::No, "unless as someone asking round their town for anyone");
    stranger.inTown = false;
    Require(StaysPut(stranger) == Stays::OutOfTown, "and never beyond its houses");
    stranger.with = true;
    Require(StaysPut(stranger) == Stays::No, "a stranger with trouble may take a chance on the stranger beside them");
    // Time skipped is not time played.
    Require(!PlayedEnough(30.0f, kTownLingerPlayed) && PlayedEnough(kTownLingerPlayed, kTownLingerPlayed),
            "a town's story begins after four minutes of play there, not after two hours waited through");
    Require(!PlayedEnough(600.0f, kStoryGapPlayed) && PlayedEnough(1200.0f, kStoryGapPlayed), "and not within twenty minutes of play of the last");
    Require(PlayedEnough(-1.0f, kStoryGapPlayed), "none begun this session: nothing to wait out");
    // Where nobody is sent (another plane, a dungeon): not even somebody the director lets come from afar.
    Going afar;
    afar.sent = true;
    afar.knows = 2;
    Require(StaysPut(afar) == Stays::No, "somebody let come from afar comes");
    afar.barred = true;
    Require(StaysPut(afar) == Stays::Barred, "but not to where nobody could have followed the player");
    afar.with = true;
    Require(StaysPut(afar) == Stays::No, "whoever is there with the player already may speak");
    Going friendly;
    friendly.knows = 2;
    Require(StaysPut(friendly) == Stays::OutOfTown, "who knows the player waits while the player is out of town");
    friendly.inTown = true;
    Require(StaysPut(friendly) == Stays::No, "and comes across town when the player is in it");
    Going heard = friendly;
    heard.knows = 1;
    Require(StaysPut(heard) == Stays::No, "so does someone whose town has heard what the player did there");
    Going sent;
    sent.sent = true;
    sent.post = true;
    Require(StaysPut(sent) == Stays::No, "who was let come by the director's rule had a reason worth the road");
    static_assert(kAcquaintedEvents > 2, "a greeting and a word back are no acquaintance");
    static_assert((Flag::Sent & (Flag::Rumored | Flag::Deferred | Flag::CameFar | Flag::Heart)) == 0, "a bit of its own");
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
        WhoGoes();
        std::cout << "ArcsCheck: all passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ArcsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
}
