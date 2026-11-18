// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/QuestsCheck.cpp
#include "Quests.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace IntelEngine::Quests;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Quest Make(Template t, std::uint32_t giver, std::uint32_t place = 500) {
    Quest q;
    q.tmpl = t;
    q.giver = giver;
    q.ally = t == Template::Venture ? giver : 0;
    q.place = place;
    q.anchor = place + 1;
    q.item = 77;
    q.stamp = "Hulda's Silver Necklace";
    q.gold = 200;
    return q;
}

// Plans and accepts a quest; returns its id.
static std::uint32_t Open(Book& book, Quest q, float now) {
    const auto id = book.Plan(std::move(q), now);
    Require(id != 0, "a quest could not be planned");
    Require(book.Accept(id, now), "a quest could not be accepted");
    return id;
}

static void KeepsakeInOrder() {
    Book book;
    const auto id = Open(book, Make(Template::Keepsake, 1), 1.0f);
    Quest& q = *book.Find(id);
    Require(q.phase == Phase::Open && q.slot != 0xFF, "an accepted quest is open with a slot");
    Require(std::abs(q.deadline - 8.0f) < 0.01f, "a keepsake stands seven days");
    Facts f;
    Require(!Advance(q, f, 1.1f).any(), "nothing true, nothing changes");
    f.inPlace = true;
    auto c = Advance(q, f, 1.2f);
    Require(c.done.size() == 1 && c.done[0] == 0 && q.step == 1, "reaching the place is the first step");
    f.inPlace = false;
    f.hasItem = true;
    c = Advance(q, f, 1.3f);
    Require(c.done.size() == 1 && c.done[0] == 1 && q.step == 2, "the item in hand is the second");
    // Being with the giver while they talk is not a handover: only the engine's move of the item is.
    f.withGiver = f.giverSpoke = true;
    Require(!Advance(q, f, 1.4f).any(), "talking to the giver does not hand the item over");
    f.handedOver = true;
    f.hasItem = false;
    c = Advance(q, f, 1.5f);
    Require(c.ending == Ending::Done && q.phase == Phase::Ended, "the handover ends it done");
}

static void KeepsakeOutOfOrder() {
    Book book;
    const auto id = Open(book, Make(Template::Keepsake, 1), 1.0f);
    Quest& q = *book.Find(id);
    Facts f;
    f.hasItem = true;  // found before the place was ever "reached" (the thief met on the road, say)
    auto c = Advance(q, f, 1.1f);
    Require(c.done.size() == 2 && q.step == 2 && q.doneMask == 0b11, "the item in hand completes the search too");
    // A handover straight away completes everything.
    Book other;
    const auto id2 = Open(other, Make(Template::Keepsake, 2), 1.0f);
    Quest& q2 = *other.Find(id2);
    Facts g;
    g.handedOver = true;
    c = Advance(q2, g, 1.1f);
    Require(c.done.size() == 3 && c.ending == Ending::Done, "a handover implies every step before it");
}

static void KeepsakeEndings() {
    {
        Book book;
        const auto id = Open(book, Make(Template::Keepsake, 1), 1.0f);
        Quest& q = *book.Find(id);
        Facts f;
        f.hasItem = true;
        Advance(q, f, 2.0f);
        f.hasItem = false;  // sold it, dropped it: still found and never brought back
        const auto c = Advance(q, f, 9.0f);
        Require(c.ending == Ending::Kept, "found and never returned is kept, item in hand or not");
    }
    {
        Book book;
        const auto id = Open(book, Make(Template::Keepsake, 1), 1.0f);
        Quest& q = *book.Find(id);
        Facts f;
        Require(Advance(q, f, 9.0f).ending == Ending::Ignored, "never found is ignored");
    }
    {
        Book book;
        const auto id = Open(book, Make(Template::Keepsake, 1), 1.0f);
        Quest& q = *book.Find(id);
        Facts f;
        f.giverDead = true;
        f.hasItem = true;
        Require(Advance(q, f, 2.0f).ending == Ending::GiverDied, "a dead giver ends it before anything else counts");
    }
}

static void BountyTalkNeedsTheDeed() {
    Book book;
    const auto id = Open(book, Make(Template::Bounty, 3), 1.0f);
    Quest& q = *book.Find(id);
    Facts f;
    f.withGiver = f.giverSpoke = true;
    Require(!Advance(q, f, 1.1f).any(), "telling the giver before the foe is dead does nothing");
    f.withGiver = f.giverSpoke = false;
    f.foeDead = true;  // by anyone's hand
    const float before = q.deadline;
    auto c = Advance(q, f, 1.2f);
    Require(c.done.size() == 1 && q.step == 1 && std::abs(q.deadline - before - 1.0f) < 0.01f,
            "the foe dead is the deed, and gives a day more to come back");
    f.withGiver = f.giverSpoke = true;
    c = Advance(q, f, 1.3f);
    Require(c.ending == Ending::Done, "then telling the giver ends it");
}

static void VentureFlow() {
    Book book;
    const auto id = Open(book, Make(Template::Venture, 9), 1.0f);
    Quest& q = *book.Find(id);
    Require(std::abs(q.deadline - 2.0f) < 0.01f, "a venture waits one day for the player");
    Facts f;
    f.inPlace = true;  // the player alone at the place is not going together
    Require(!Advance(q, f, 1.1f).any(), "going there alone is not going with them");
    f.allyInPlace = true;
    auto c = Advance(q, f, 1.2f);
    Require(c.done.size() == 1 && std::abs(q.deadline - 4.0f) < 0.01f, "together at the place, two days more");
    f.withAlly = f.allySpoke = true;
    Require(!Advance(q, f, 1.3f).any(), "sharing before it is cleared does nothing");
    f.cleared = true;
    c = Advance(q, f, 1.4f);
    // Cleared and talking in the same look: cleared is done now, the share comes on the next look.
    Require(c.done.size() == 1 && q.step == 2 && c.ending == Ending::None, "cleared first");
    c = Advance(q, f, 1.5f);
    Require(c.ending == Ending::Done, "then the share ends it");
    {
        Book b2;
        const auto id2 = Open(b2, Make(Template::Venture, 9), 1.0f);
        Facts g;
        Require(Advance(*b2.Find(id2), g, 2.5f).ending == Ending::WentAlone, "the player never came: they went alone");
    }
    {
        Book b3;
        const auto id3 = Open(b3, Make(Template::Venture, 9), 1.0f);
        Quest& q3 = *b3.Find(id3);
        Facts g;
        g.inPlace = g.allyInPlace = true;
        Advance(q3, g, 1.1f);
        g.inPlace = g.allyInPlace = false;
        c = Advance(q3, g, 10.0f);
        Require(c.ending == Ending::None && q3.phase == Phase::Open && q3.deadline > 10.0f, "together and at it: the time moves on");
        g.allyDead = true;
        Require(Advance(q3, g, 10.1f).ending == Ending::AllyDied, "the ally's death ends it");
    }
}

static void VentureCompanion() {
    Book book;
    auto q = Make(Template::Venture, 9);
    q.companion = 12;
    const auto id = book.Plan(q, 1.0f);
    Require(id != 0, "planned");
    Require(book.Plan(Make(Template::Keepsake, 12, 600), 1.1f) == 0, "the companion is in a quest already");
    Require(!CompanionLines(*book.Find(id)).empty() && CompanionLines(Make(Template::Venture, 9)).empty(),
            "a companion knows of the venture; none without one");
    Require(OfferPrompt(*book.Find(id)).find("{companion}") != std::string::npos, "the offer names both");
    Require(FellInMemory(true).find("{ally}") != std::string::npos && FellInMemory(false).find("{companion}") != std::string::npos,
            "each remembers the other");
    Require(book.Accept(id, 1.2f), "accepted");
    Facts f;
    Require(Advance(*book.Find(id), f, 3.0f).ending == Ending::WentAlone, "never came: they went alone");
    Require(CompanionLines(*book.Find(id)).find("went on our own") != std::string::npos, "and the companion remembers it");
}

static void BookRules() {
    Book book;
    const auto a = book.Plan(Make(Template::Keepsake, 1), 1.0f);
    Require(a != 0, "a first quest is planned");
    Require(book.Plan(Make(Template::Bounty, 2), 1.0f) == 0, "one planned quest at a time");
    Require(book.Accept(a, 1.1f), "accepted");
    Require(book.Plan(Make(Template::Bounty, 1), 1.2f) == 0, "one live quest per giver");
    const auto b = book.Plan(Make(Template::Bounty, 2, 600), 1.2f);
    Require(b != 0 && book.Accept(b, 1.2f), "a second giver");
    const auto c = book.Plan(Make(Template::Bounty, 3, 700), 1.3f);
    Require(c != 0 && book.Accept(c, 1.3f), "a third");
    Require(!book.Refusal(4).empty(), "three open is the most");
    // Slots go round, so a completed quest keeps its place in the journal as long as possible.
    Require(book.Find(a)->slot == 0 && book.Find(b)->slot == 1 && book.Find(c)->slot == 2, "slots in turn");
    Facts f;
    f.giverDead = true;
    Advance(*book.Find(a), f, 1.4f);
    const auto d = book.Plan(Make(Template::Keepsake, 4, 800), 1.5f);
    Require(d != 0 && book.Accept(d, 1.5f) && book.Find(d)->slot == 3, "the next slot, not the one just freed");
    Require(book.Find(a)->slot == 0, "the ended quest keeps its slot until reused");
    // Lapse: a planned quest whose giver never came.
    const auto e = book.Plan(Make(Template::Keepsake, 5, 900), 2.0f);
    Require(e == 0, "three open again: refused");
    Facts g;
    g.foeDead = true;
    Advance(*book.Find(b), g, 2.0f);
    g.withGiver = g.giverSpoke = true;
    Advance(*book.Find(b), g, 2.0f);
    const auto e2 = book.Plan(Make(Template::Keepsake, 5, 900), 2.0f);
    Require(e2 != 0, "room again");
    Require(book.Lapse(2.5f).empty(), "not lapsed within the day");
    const auto lapsed = book.Lapse(3.1f);
    Require(lapsed.size() == 1 && book.Find(e2)->ending == Ending::Lapsed, "lapsed after a day");
    Require(!book.Accept(e2, 3.2f), "a lapsed quest cannot be taken on");
    // Declining.
    const auto f2 = book.Plan(Make(Template::Bounty, 6, 950), 3.3f);
    Require(book.Decline(f2, 3.3f) && book.Find(f2)->ending == Ending::Declined, "declined");
}

static void Choices() {
    Book book;
    std::vector<PlaceOption> places = {
        {100, 101, false, false, false, 1000.0f},
        {200, 201, true, false, true, 50000.0f},
        {300, 301, true, true, false, 20000.0f},
        {400, 0, true, false, false, 10.0f},  // no anchor: never
    };
    const auto* p = PickPlace(places, book, 0, 7);
    Require(p && p->inGiverHold && p->anchor != 0, "a place of the giver's hold with a boss room");
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        const auto* x = PickPlace(places, book, 300, seed);
        Require(x && x->location == 200, "never the place the player is in");
    }
    const auto* cleared = PickPlace(places, book, 0, 3, true);
    Require(cleared && cleared->location == 300, "a cleared place when one is wanted");
    auto q = Make(Template::Bounty, 1, 200);
    const auto id = book.Plan(q, 1.0f);
    Require(id != 0, "planned");
    for (std::uint32_t seed = 0; seed < 50; ++seed) {
        const auto* x = PickPlace(places, book, 0, seed);
        Require(x && x->location != 200, "never a place another quest uses");
    }
    Require(PickTemplate(true, true, Template::Keepsake, 1) == Template::Bounty, "not the same kind twice running");
    Require(PickTemplate(true, false, Template::Keepsake, 1) == Template::Keepsake, "unless it is the only one that fits");
    Require(PickTemplate(false, false, Template::None, 1) == Template::None, "nothing fits: no quest");
    Require(FoeName(5, false) == FoeName(5, false), "a name is the same for the same seed");
    Require(ShortName("Uthgerd the Unbroken") == "Uthgerd" && ShortName("Olfina Gray-Mane") == "Olfina Gray-Mane" &&
                ShortName("the Gaunt") == "the Gaunt",
            "short names before 's");
    Require(PickTruth(Template::Venture, places[2], false, 1) == Truth::PlaceRetaken, "a cleared place was retaken");
    Require(PickTruth(Template::Bounty, places[1], false, 1) == Truth::FoePlain, "spoils only with someone to return them to");
    int dead = 0;
    for (std::uint32_t seed = 0; seed < 1000; ++seed) dead += PickTruth(Template::Keepsake, places[0], false, seed) == Truth::ThiefDead;
    Require(dead > 200 && dead < 400, "about three in ten thieves lie dead");
    Require(RewardGold(Template::Bounty, 10) > RewardGold(Template::Keepsake, 10), "a foe pays more than a lost thing");
    Require(EndStage(Template::Keepsake, Ending::Done) == 190 && EndStage(Template::Venture, Ending::WentAlone) == 394 &&
                EndStage(Template::Bounty, Ending::Declined) == 0,
            "the journal stages");
}

static void SaveData() {
    Book book;
    auto q = Make(Template::Keepsake, 1);
    q.truth = Truth::ThiefDead;
    q.foeName = "Grimvar Half-Hand";
    q.title = "Hulda's Silver Necklace";
    q.placed = Placed::Foe | Placed::Gang;
    q.rumor = 42;
    q.handed = true;
    const auto id = Open(book, q, 1.0f);
    Facts f;
    f.inPlace = true;
    Advance(*book.Find(id), f, 1.5f);
    book.Plan(Make(Template::Venture, 9, 600), 1.6f);
    const auto bytes = Serialize(book);
    Book read;
    Require(Deserialize(bytes, read), "a round trip loads");
    Require(read.all.size() == 2 && read.nextId == book.nextId && read.lastSlot == book.lastSlot, "the book comes back");
    const auto* r = read.Find(id);
    Require(r && r->truth == Truth::ThiefDead && r->step == 1 && r->doneMask == 1 && r->stamp == q.stamp &&
                r->foeName == q.foeName && r->title == q.title && r->slot == book.Find(id)->slot && r->placed == q.placed &&
                r->rumor == 42 && r->handed,
            "a quest comes back as it was");
    auto broken = bytes;
    broken.pop_back();
    Require(!Deserialize(broken, read), "a short record does not load");
    auto bad = bytes;
    bad[4 + 1 + 1 + 4 + 4] = 9;  // the first quest's template
    Book untouched;
    Require(!Deserialize(bad, untouched) && untouched.all.empty(), "a damaged record loads nothing");
    Require(!Deserialize(bytes, read, 2), "an unknown version does not load");
}

int main() {
    try {
        KeepsakeInOrder();
        KeepsakeOutOfOrder();
        KeepsakeEndings();
        BountyTalkNeedsTheDeed();
        VentureFlow();
        VentureCompanion();
        BookRules();
        Choices();
        SaveData();
    } catch (const std::exception& e) {
        std::cerr << "QuestsCheck FAILED: " << e.what() << '\n';
        return 1;
    }
    std::cout << "QuestsCheck passed\n";
    return 0;
}
