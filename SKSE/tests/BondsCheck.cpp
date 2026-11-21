// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/BondsCheck.cpp
#include "Bonds.h"
#include "Hearts.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::Bonds;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void WhenToRead() {
    const Bond never;
    Require(!Due(never, 0, 10.0f) && !Due(never, kFirstAt - 1, 10.0f), "a greeting and a word back are nothing to read");
    Require(Due(never, kFirstAt, 10.0f), "once enough has passed between the two it is read");
    Bond read = never;
    Require(Read("friend", "none", "unanswered", 0.9f, read, 6, 10.0f) == Reading::Taken, "a named standing the reader is sure of is taken");
    Require(!Due(read, 6, 10.5f) && !Due(read, 6 + kAgainAfter - 1, 12.0f), "not read again until much more has passed between them");
    Require(!Due(read, 6 + kAgainAfter, 10.0f + 1.0f / 24.0f), "nor within two game hours, however much they talk");
    Require(Due(read, 6 + kAgainAfter, 10.0f + 2.5f / 24.0f), "then it is read again");
    Require(Due(read, 6, 9.0f), "and after the clock was set back");
    Require(ByCount(kFirstAt - 1) == Stranger && ByCount(kFirstAt) == Acquaintance && ByCount(500) == Acquaintance,
            "a count says acquainted and no more: a quarrel counts as much as a kindness");
    const Bond empty = NothingToRead(never, 9, 4.0f);
    Require(Judged(empty) && empty.standing == Acquaintance && !empty.drawn && empty.eventsAt == 9 && !Due(empty, 12, 5.0f),
            "nothing on record to read: acquainted by the count, looked at again later");
    const Bond kept = NothingToRead(read, 30, 20.0f);
    Require(kept.standing == Friend, "and an earlier reading is not thrown away for it");
}

static bool Took(Reading r) { return r == Reading::Taken; }

static void WhatWasRead() {
    const float sure = 0.9f;
    Bond b;
    Require(Read("warm", "drawn", "welcomed", sure, b, 6, 1.0f) == Reading::Unreadable && !Judged(b), "an answer that is none of the names changes nothing");
    Require(Took(Read("confidant", "drawn", "unanswered", sure, b, 7, 2.0f)), "a close friend who is drawn to the player");
    Require(b.standing == Confidant && b.drawn && b.answer == Unanswered && b.declinedAt < 0.0f && Close(b) && Knows(b) && WouldAsk(b),
            "and the player has not answered");
    Require(Took(Read("friend", "drawn", "declined", sure, b, 20, 5.0f)) && b.answer == Declined && b.declinedAt == 5.0f, "the player says no: when is kept");
    Require(Took(Read("friend", "none", "unanswered", sure, b, 40, 9.0f)) && b.drawn && b.answer == Declined && b.declinedAt == 5.0f,
            "a no stands through a later reading that finds nothing of the heart");
    Require(Took(Read("friend", "drawn", "unanswered", sure, b, 60, 12.0f)) && b.answer == Declined && b.declinedAt == 5.0f,
            "and through one that finds it unanswered");
    Require(Took(Read("confidant", "drawn", "welcomed", sure, b, 80, 50.0f)) && b.answer == Welcomed && b.declinedAt < 0.0f,
            "only the player taking it back ends it");
    Require(Took(Read("lover", "none", "declined", sure, b, 90, 60.0f)) && b.standing == Lover && b.drawn && b.answer == Welcomed,
            "lovers by the word of both: the heart is in it and welcome");

    Bond sour;
    Require(Took(Read("cold", "none", "unanswered", sure, sour, 8, 1.0f)) && sour.sour == Cold && sour.standing == Acquaintance, "somebody who dislikes the player");
    Require(Knows(sour) && !Close(sour) && !WouldAsk(sour), "knows them, is not close, and does not bring their trouble to them");
    Require(Took(Read("foe", "drawn", "welcomed", sure, sour, 20, 3.0f)) && sour.sour == Foe && sour.standing == Acquaintance, "a foe is no friend, whatever else was read");
    Require(Took(Read("friend", "none", "unanswered", sure, sour, 40, 6.0f)) && sour.sour == NotSour && WouldAsk(sour), "and they may make it up");

    Bond stranger;
    Require(Took(Read("stranger", "none", "unanswered", sure, stranger, 6, 1.0f)) && Judged(stranger) && !Knows(stranger),
            "often in the same room and nothing between them: read, and still a stranger");
}

static void OneReadingIsNotBelievedOnItsWord() {
    // Played 2026-11-21: one and the same talk with an innkeeper, read three times: friend, acquaintance, friend.
    const float unsure = 0.55f;
    Bond b;
    Require(Read("friend", "none", "unanswered", unsure, b, 6, 1.0f) == Reading::Noted, "said once, and not sure: noted");
    Require(Judged(b) && b.standing == Acquaintance && !b.drawn && b.noted != kNothingNoted,
            "they are acquainted by the count meanwhile, and what was said is kept in mind");
    Require(Due(b, 6 + kConfirmAfter, 1.0f + 1.0f / 24.0f) && !Due(b, 6 + kConfirmAfter - 1, 2.0f), "and they are read again sooner than otherwise");
    Require(Read("acquaintance", "none", "unanswered", unsure, b, 9, 1.1f) == Reading::Taken && b.standing == Acquaintance && b.noted == kNothingNoted,
            "a reading that says what already stands is taken, and nothing is left in mind");
    Require(Read("friend", "none", "unanswered", unsure, b, 12, 1.2f) == Reading::Noted && b.standing == Acquaintance, "friend again, unsure: noted");
    Require(Read("friend", "none", "unanswered", unsure, b, 15, 1.3f) == Reading::Taken && b.standing == Friend, "said twice running: believed");
    Require(Read("acquaintance", "none", "unanswered", unsure, b, 18, 1.4f) == Reading::Noted && b.standing == Friend, "a friend is not lost to one unsure reading");
    Require(Read("friend", "none", "unanswered", unsure, b, 21, 1.5f) == Reading::Taken && b.standing == Friend && b.noted == kNothingNoted,
            "and the doubt is gone when the next one says friend");
    // The heart, likewise: nothing of the heart is believed on one unsure word.
    Require(Read("friend", "drawn", "unanswered", unsure, b, 24, 1.6f) == Reading::Noted && !b.drawn, "drawn to the player, said once and unsure: not believed yet");
    Require(Read("friend", "drawn", "welcomed", unsure, b, 27, 1.7f) == Reading::Noted && !b.drawn, "the next one differs in the answer: still not");
    Require(Read("friend", "drawn", "welcomed", unsure, b, 30, 1.8f) == Reading::Taken && b.drawn && b.answer == Welcomed, "twice the same: believed");
    Bond sure;
    Require(Read("lover", "drawn", "welcomed", kSure, sure, 40, 2.0f) == Reading::Taken && sure.standing == Lover, "a reading the reader is sure of is enough by itself");
    // What a reading said, as one number: everything that makes two readings differ.
    Bond x, y;
    Read("friend", "drawn", "unanswered", 1.0f, x, 6, 1.0f);
    Read("friend", "drawn", "welcomed", 1.0f, y, 6, 1.0f);
    Require(Code(x) != Code(y) && Code(x) != kNothingNoted && Code(y) != kNothingNoted, "two readings that differ have two numbers");
}

static void TheSameLadderAsHearts() {
    using namespace IntelEngine;
    static_assert(static_cast<int>(Bonds::Stranger) == static_cast<int>(Hearts::Stranger));
    static_assert(static_cast<int>(Bonds::Acquaintance) == static_cast<int>(Hearts::Acquaintance));
    static_assert(static_cast<int>(Bonds::Friend) == static_cast<int>(Hearts::Friend));
    static_assert(static_cast<int>(Bonds::Confidant) == static_cast<int>(Hearts::Confidant));
    static_assert(static_cast<int>(Bonds::Lover) == static_cast<int>(Hearts::Lover));
    // A friend who is drawn to the player, read from memories alone, gives the same story a relationship mod's
    // tier would: a courtship. And a no closes it the same way.
    Hearts::Who who;
    who.person = true;
    Hearts::Heart h;
    h.tier = Bonds::Friend;
    h.sparked = true;
    Require(Hearts::LeanOf(who, h, Hearts::Rules{}) == Hearts::Lean::Courtship, "a friend who is drawn to the player: courtship");
    h.stance = Bonds::Declined;
    h.refusedDays = 3.0f;
    Require(Hearts::LeanOf(who, h, Hearts::Rules{}) == Hearts::Lean::None, "turned down: nothing");
}

static void WhoIsReadNext() {
    Candidate far{1, 40, 40, false, false}, here{2, 8, 8, false, true}, with{3, 6, 6, true, false}, more{4, 90, 30, false, true};
    Require(Before(with, here) && Before(with, far) && Before(with, more), "whoever is with the player first");
    Require(Before(here, far) && Before(more, far), "then people of the town the player is in");
    Require(Before(more, here), "then whoever has had most to do with the player since their last reading");
    Require(!Before(far, far), "nobody before themselves");
}

static void KeptWithTheSave() {
    Book book;
    Bond a, b;
    Read("confidant", "drawn", "declined", 1.0f, a, 31, 12.5f);
    Read("foe", "none", "unanswered", 1.0f, b, 9, 3.25f);
    b.noted = 0x12;
    a.times = 14;
    book.emplace(0x0001A66Eu, a);
    book.emplace(0xFE012345u, b);
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back) && back.size() == 2, "the book comes back");
    const Bond& x = back.at(0x0001A66Eu);
    Require(x.standing == Confidant && x.drawn && x.answer == Declined && x.judgedAt == 12.5f && x.declinedAt == 12.5f && x.eventsAt == 31,
            "with every field");
    Require(back.at(0xFE012345u).sour == Foe && back.at(0xFE012345u).judgedAt == 3.25f && back.at(0xFE012345u).noted == 0x12, "for each of them");
    Require(x.noted == kNothingNoted && x.times == 14, "and nothing in mind where nothing was; the count of their lines to the player too");
    Bond counted;
    counted.times = 5;
    Require(Read("friend", "none", "unanswered", 1.0f, counted, 6, 1.0f) == Reading::Taken && counted.times == 5, "a reading leaves the count alone");
    Require(NothingToRead(counted, 9, 2.0f).times == 5, "so does a look that finds nothing to read");
    auto shortened = bytes;
    shortened.pop_back();
    Book untouched = back;
    Require(!Deserialize(shortened, untouched) && untouched.size() == 2, "a short record is refused and leaves the book as it was");
    auto wrong = bytes;
    wrong[8] = 9;   // a standing that does not exist
    Require(!Deserialize(wrong, untouched), "a record that cannot be is refused");
    Book none;
    Require(Deserialize(Serialize(Book{}), none) && none.empty(), "an empty book is a book");
}

int main() {
    try {
        WhenToRead();
        WhatWasRead();
        OneReadingIsNotBelievedOnItsWord();
        TheSameLadderAsHearts();
        WhoIsReadNext();
        KeptWithTheSave();
    } catch (const std::exception& e) {
        std::cerr << "BondsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "BondsCheck passed\n";
    return 0;
}
