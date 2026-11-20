// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/HeartsCheck.cpp
#include "Hearts.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::Hearts;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Who Adult() {
    Who w;
    w.person = true;
    return w;
}

static void WhoMayBePartOfOne() {
    const Rules rules;
    Heart warm;
    warm.tier = Confidant;
    warm.sparked = true;
    warm.stance = 1;
    Require(LeanOf(Adult(), warm, rules) == Lean::Courtship, "a spark between friends: courtship");

    // The hard rules hold whatever the bond says.
    Who child = Adult();
    child.childlike = true;
    Require(LeanOf(child, warm, rules) == Lean::None, "never a child, nor anyone who looks or sounds like one");
    Heart wife = warm;
    wife.wed = Wed::Married;
    Require(LeanOf(child, wife, rules) == Lean::None, "not even when some mod says they are wed");
    Who kin = Adult();
    kin.kin = true;
    Require(LeanOf(kin, warm, rules) == Lean::None, "never the player's own family");
    Who creature;
    Require(LeanOf(creature, warm, rules) == Lean::None, "only people of the world");
    Heart refused = warm;
    refused.stance = -1;
    Require(LeanOf(Adult(), refused, rules) == Lean::None, "turned down is turned down");
    refused.tier = Beloved;
    Require(LeanOf(Adult(), refused, rules) == Lean::None, "however strong it is on their side");
    // A no stands for a season; after it, only where the two have grown close, the door may stand ajar again.
    Heart later = warm;
    later.stance = -1;
    later.tier = Confidant;
    later.refusedDays = kRefusalRestDays - 1.0f;
    Require(LeanOf(Adult(), later, rules) == Lean::None, "a no still stands inside its season");
    later.refusedDays = kRefusalRestDays;
    Require(SecondChance(later) && LeanOf(Adult(), later, rules) == Lean::Courtship, "long after, and close again: a second chance");
    later.tier = Friend;
    Require(LeanOf(Adult(), later, rules) == Lean::None, "time alone reopens nothing: they must have grown closer than before");
    later.tier = Confidant;
    later.sparked = false;
    Require(LeanOf(Adult(), later, rules) == Lean::None, "nothing on their side any more: nothing to reopen");
    later.sparked = true;
    later.wedElsewhere = true;
    Require(LeanOf(Adult(), later, rules) == Lean::None, "married to another since: never a second chance");
    later.wedElsewhere = false;
    later.refusedDays = -1.0f;
    Require(LeanOf(Adult(), later, rules) == Lean::None, "when the no was said is not known: it stands");
    later.refusedDays = 90.0f;
    Require(Words(later, "Ada").find("turned them down once") != std::string::npos && Words(later, "Ada").find("never pressed") != std::string::npos &&
            Words(later, "Ada").find("not reopened") == std::string::npos, "a second chance is said as what it is");
    Require(LeanOf(child, later, rules) == Lean::None, "and never a child, whatever else holds");
    Rules classic;
    classic.stories = false;
    Require(LeanOf(Adult(), warm, classic) == Lean::None, "switched off: none");
}

static void WhatItWouldBeAbout() {
    const Rules rules;
    Heart h;
    Require(LeanOf(Adult(), h, rules) == Lean::None, "a stranger: nothing");
    h.tier = Confidant;
    Require(LeanOf(Adult(), h, rules) == Lean::None, "a close friend without a spark: friendship, no story of the heart");
    h.sparked = true;
    h.tier = Acquaintance;
    Require(LeanOf(Adult(), h, rules) == Lean::None, "a spark with someone barely known: too soon");
    h.tier = Friend;
    Require(LeanOf(Adult(), h, rules) == Lean::Courtship, "unanswered still counts: the story is how it gets answered");
    h.tier = Lover;
    Require(LeanOf(Adult(), h, rules) == Lean::Together, "lovers: the life they share");
    Heart betrothed;
    betrothed.wed = Wed::Engaged;
    Require(LeanOf(Adult(), betrothed, rules) == Lean::Together, "betrothed");
    Heart spouse;
    spouse.wed = Wed::Married;
    Require(LeanOf(Adult(), spouse, rules) == Lean::Together, "wed, no children");
    spouse.children = 2;
    Require(LeanOf(Adult(), spouse, rules) == Lean::Family, "wed with children: the household");
    spouse.children = 0;
    spouse.withChild = 2;
    Require(LeanOf(Adult(), spouse, rules) == Lean::Together, "with child by another: not the player's household story");
    spouse.childIsPlayers = true;
    Require(LeanOf(Adult(), spouse, rules) == Lean::Family, "the player's child on the way");
    Heart over;
    over.wed = Wed::Ended;
    over.tier = Lover;
    over.sparked = true;
    Require(LeanOf(Adult(), over, rules) == Lean::None, "what ended is not reopened by a story");
}

static void Affairs() {
    Heart h;
    h.tier = Confidant;
    h.sparked = true;
    h.stance = 1;
    h.wedElsewhere = true;
    Rules rules;
    Require(LeanOf(Adult(), h, rules) == Lean::None, "married to another: nothing, unless affairs are told at all");
    rules.affairs = true;
    Require(LeanOf(Adult(), h, rules) == Lean::Affair, "welcomed by the player, close already: an affair");
    h.stance = 0;
    Require(LeanOf(Adult(), h, rules) == Lean::None, "never before the player has welcomed it");
    h.stance = 1;
    h.tier = Friend;
    Require(LeanOf(Adult(), h, rules) == Lean::None, "never as a first step");
    h.tier = Lover;
    h.sparked = false;
    Require(LeanOf(Adult(), h, rules) == Lean::None, "and never without a spark");
}

static void WithoutARelationshipMod() {
    // The game's own rank and its own list of people one can marry.
    Require(LeanOf(Adult(), FromTheGame(0, true, false, false), Rules{}) == Lean::None, "an acquaintance one could marry: nothing yet");
    Require(LeanOf(Adult(), FromTheGame(1, true, false, false), Rules{}) == Lean::Courtship, "a friend the game lets one marry: courtship");
    Require(LeanOf(Adult(), FromTheGame(3, false, false, false), Rules{}) == Lean::None, "an ally the game does not let one marry: friendship");
    Require(LeanOf(Adult(), FromTheGame(4, false, true, false), Rules{}) == Lean::Together, "the spouse");
    Require(FromTheGame(2, true, false, true).sparked == false, "someone else's spouse has no spark by the game's account");
    Require(!FromTheGame(1, true, false, false).tracked, "and nothing here claims a mod keeps it");
}

static void WordsForTheAuthor() {
    Heart h;
    Require(Words(h, "Ada").empty(), "nothing to say of a stranger");
    h.tier = Acquaintance;
    Require(Words(h, "Ada").empty(), "nor of an acquaintance");
    h.tier = Friend;
    Require(Words(h, "Ada") == "To Ada: a friend.", "a friend");
    h.sparked = true;
    Require(Words(h, "Ada").find("more than friendship") != std::string::npos && Words(h, "Ada").find("not yet answered") != std::string::npos,
            "a spark, unanswered");
    h.stance = -1;
    Require(Words(h, "Ada").find("turned them down") != std::string::npos && Words(h, "Ada").find("not reopened") != std::string::npos,
            "turned down is said, so nobody writes around it");
    Heart wife;
    wife.wed = Wed::Married;
    wife.tier = Beloved;
    wife.withChild = 3;
    wife.childIsPlayers = true;
    wife.children = 1;
    const auto said = Words(wife, "Ada");
    Require(said.find("Married to Ada.") == 0 && said.find("near her time") != std::string::npos && said.find("the child is Ada's") != std::string::npos &&
                said.find("Has a child with Ada.") != std::string::npos && said.find("To Ada:") == std::string::npos,
            "a wife with child, and a child already");
    Heart other;
    other.tier = Friend;
    other.wedElsewhere = true;
    Require(Words(other, "Ada").find("Married to someone else.") != std::string::npos, "someone else's spouse is named as such");
}

static void GrowingCloserOrApart() {
    Require(RankAfter(0, true) == 1 && RankAfter(2, true) == 3, "closer: one step");
    Require(RankAfter(3, true) == 3 && RankAfter(4, true) == 4, "never to lover by a story, and a lover stays one");
    Require(RankAfter(2, false) == 1 && RankAfter(0, false) == 0 && RankAfter(-2, false) == -2, "apart: one step, never into enmity");
}

int main() {
    try {
        WhoMayBePartOfOne();
        WhatItWouldBeAbout();
        Affairs();
        WithoutARelationshipMod();
        WordsForTheAuthor();
        GrowingCloserOrApart();
    } catch (const std::exception& e) {
        std::cerr << "HeartsCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "HeartsCheck passed\n";
    return 0;
}
