// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/StorylinesCheck.cpp
#include "Storylines.h"

#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>

using namespace IntelEngine::Storylines;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static const Storyline& At(const Book& book, std::uint32_t id, const char* what) {
    const Storyline* s = book.Find(id);
    if (!s) throw std::runtime_error(std::string("no storyline for: ") + what);
    return *s;
}

static Storyline Meeting(std::uint32_t owner, std::uint32_t place, float deadline) {
    Storyline s;
    s.kind = Kind::Meeting;
    s.owner = owner;
    s.place = place;
    s.target = place + 1;
    s.deadline = deadline;
    return s;
}

static Storyline Ask(std::uint32_t owner, Request request, std::uint32_t other = 0) {
    Storyline s;
    s.kind = Kind::Request;
    s.request = request;
    s.owner = owner;
    s.other = other;
    s.deadline = 30.0f;
    s.importance = 2;
    return s;
}

static void Meetings() {
    Book book;
    const auto id = book.Open(Meeting(1, 100, 10.5f), 10.0f);
    Require(id != 0, "a meeting opens");
    const Storyline* s = &At(book, id, "id");
    Require(s->slot == 0 && ObjectiveFor(*s) == Objective::Meet, "it gets the first slot and shows Meet");
    Require(book.Unresolved(1), "an open meeting is unfinished business");
    Require(book.Overdue(10.4f).empty() && book.Overdue(10.5f).size() == 1, "overdue once the time is up");
    Require(!book.Apply(id, Event::TimeRanOut, 10.4f), "the time cannot run out early");
    Require(book.Apply(id, Event::TimeRanOut, 10.6f) && At(book, id, "id").stage == Stage::Ignored, "an unkept meeting is ignored");
    Require(At(book, id, "id").slot == 0 && ObjectiveFor(At(book, id, "id")) == Objective::None, "and shows nothing any more");
    Require(OwnerMemory(At(book, id, "id")) == "I waited at {place} for {player}, who never came.", "remembered as such");
    Require(book.Unresolved(1), "and stays unfinished business");
    book.MarkFollowedUp(1);
    Require(!book.Unresolved(1), "until the director came back to it");
    Require(!book.Apply(id, Event::Met, 11.0f), "an ended storyline ignores later events");

    const auto kept = book.Open(Meeting(2, 200, 20.0f), 11.0f);
    Require(At(book, kept, "kept").slot == 0, "a freed slot is used again");
    Require(book.Apply(kept, Event::Met, 12.0f) && At(book, kept, "kept").stage == Stage::Done, "a kept meeting is done");
    Require(OwnerMemory(At(book, kept, "kept")) == "I met {player} at {place} as we had agreed.", "and remembered");
    Require(PublicDeed(At(book, kept, "kept")).empty(), "a meeting is private: no deed people hear of");
    const auto noShow = book.Open(Meeting(3, 300, 30.0f), 12.0f);
    Require(book.Apply(noShow, Event::NoShow, 12.5f) && At(book, noShow, "noShow").stage == Stage::Ignored,
            "the meeting system says the player never came: ignored, deadline or not");
    const auto fell = book.Open(Meeting(4, 400, 30.0f), 12.0f);
    Require(book.Apply(fell, Event::FellThrough, 12.5f) && At(book, fell, "fell").stage == Stage::Failed &&
                OwnerMemory(At(book, fell, "fell")).empty(), "it fell through on their side: failed, nothing on the player");
    const auto req = book.Open(Ask(5, Request::FindPerson), 12.0f);
    Require(!book.Apply(req, Event::NoShow, 12.5f), "a no-show means nothing to a request");
}

static void Requests() {
    Book book;
    const auto find = book.Open(Ask(1, Request::FindPerson, 7), 1.0f);
    Require(ObjectiveFor(At(book, find, "find")) == Objective::Find, "find someone");
    Require(book.Apply(find, Event::Achieved, 2.0f) && ObjectiveFor(At(book, find, "find")) == Objective::ReturnTo,
            "found: go back to the one who asked");
    Require(!book.Apply(find, Event::Achieved, 2.5f), "achieving twice changes nothing");
    Require(book.Apply(find, Event::Met, 3.0f) && At(book, find, "find").stage == Stage::Done, "back with them: done");
    Require(RewardGold(At(book, find, "find"), 10) == 180, "a real request at level 10 pays 180 gold");
    Require(OwnerMemory(At(book, find, "find")) == "I asked {player} to find {other}, and {player} did it.", "remembered");

    const auto escort = book.Open(Ask(2, Request::EscortMe), 1.0f);
    Require(ObjectiveFor(At(book, escort, "escort")) == Objective::Escort, "escort shows Escort");
    Require(!book.Apply(escort, Event::Met, 1.5f), "being together is not arriving");
    Require(book.Apply(escort, Event::Achieved, 2.0f) && At(book, escort, "escort").stage == Stage::Done, "arrived: done at once");

    const auto word = book.Open(Ask(3, Request::DeliverWord, 9), 1.0f);
    Require(ObjectiveFor(At(book, word, "word")) == Objective::SpeakWith, "deliver word: speak with them");
    Require(book.Apply(word, Event::Talked, 2.0f) && ObjectiveFor(At(book, word, "word")) == Objective::ReturnTo, "spoken: go back");

    const auto rescue = book.Open(Ask(4, Request::Rescue, 11), 1.0f);
    Require(book.Apply(rescue, Event::SomeoneDied, 2.0f) && At(book, rescue, "rescue").stage == Stage::Failed, "the captive died");
    Require(OwnerMemory(At(book, rescue, "rescue")).find("ended badly") != std::string::npos, "remembered as a failure");
    Require(book.Unresolved(4), "a failure is unfinished business");

    const auto threat = book.Open(Ask(5, Request::DealWithThreat), 1.0f);
    Require(ObjectiveFor(At(book, threat, "threat")) == Objective::GoTo, "a threat without a named foe: go to the place");
    Require(book.Apply(threat, Event::Declined, 1.1f) && At(book, threat, "threat").stage == Stage::Declined, "the player said no");
    Require(OwnerMemory(At(book, threat, "threat")) == "I asked {player} to deal with the trouble at {place}, and {player} refused.",
            "remembered as a refusal");
    Require(RewardGold(At(book, threat, "threat"), 10) == 0, "no gold for a refusal");
}

static void Variety() {
    Book book;
    Require(book.Open(Ask(1, Request::FindPerson), 1.0f) != 0, "first request");
    Require(book.Open(Ask(1, Request::BringItem), 1.0f) == 0, "never two open from the same person");
    Require(book.Open(Ask(2, Request::FindPerson), 1.0f) != 0, "second request of the same kind");
    Require(!book.Refusal(Kind::Request, Request::FindPerson, 3, false).empty(), "not the same kind three times running");
    Require(book.Refusal(Kind::Request, Request::FindPerson, 3, true).empty(), "unless it is urgent");
    Require(book.Refusal(Kind::Request, Request::BringItem, 3, false).empty(), "another kind of request is fine");
    Require(book.Open(Meeting(3, 1, 9.0f), 1.0f) != 0, "a meeting in between");
    Require(book.Open(Ask(4, Request::Rescue), 1.0f) != 0, "then a request again");
    for (int i = 0; i < 3; ++i) book.Open(Meeting(40 + i, 1, 9.0f), 1.0f);  // fills up; meetings never hit the variety rule
    Require(book.OpenCount() == 6, "meetings are not refused for variety");
    Require(book.Open(Meeting(50, 1, 9.0f), 1.0f) == 0, "no seventh");
    book = Book{};
    Storyline visit;
    visit.kind = Kind::Visit;
    visit.owner = 5;
    const auto v = book.Open(visit, 1.0f);
    Require(v != 0 && At(book, v, "v").slot == 0xFF && ObjectiveFor(At(book, v, "v")) == Objective::None, "a visit takes no slot");
    for (int i = 0; i < 5; ++i) book.Open(Meeting(60 + i, 1, 9.0f), 1.0f);
    Require(book.OpenCount() == 6 && book.Open(Meeting(70, 1, 9.0f), 1.0f) == 0, "six open, no seventh");
    std::array<bool, kSlots> used{};
    for (const auto& s : book.all) {
        if (s.slot != 0xFF) {
            Require(!used[s.slot], "no two open storylines share a slot");
            used[s.slot] = true;
        }
    }
}

static void SaveData() {
    Book book;
    book.Open(Meeting(1, 100, 10.5f), 10.0f);
    const auto r = book.Open(Ask(2, Request::BringItem, 0), 10.0f);
    Require(r != 0, "the request opens");
    book.Find(r)->item = 0x1234;
    book.Apply(r, Event::Achieved, 11.0f);
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "round trip");
    Require(back.all.size() == 2 && back.nextId == book.nextId && back.opened == book.opened, "everything comes back");
    const auto* s = back.Find(r);
    Require(s && s->stage == Stage::ReturnTo && s->item == 0x1234 && s->slot == 1 && s->request == Request::BringItem,
            "the request with its stage, item and slot");
    Book untouched;
    untouched.nextId = 77;
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        Require(!Deserialize(std::span(bytes.data(), cut), untouched), "short data is refused");
    }
    Require(untouched.nextId == 77 && untouched.all.empty(), "and changes nothing");
    auto damaged = bytes;
    damaged[11 + 4] = 99;  // the first storyline's kind (after an 11-byte header and its id)
    Require(!Deserialize(damaged, untouched), "a damaged kind is refused");
}

static void Deeds() {
    Storyline s;
    s.kind = Kind::Request;
    s.request = Request::BringItem;
    s.owner = 1;
    s.item = 5;
    s.stage = Stage::Open;
    Require(PublicDeed(s).empty(), "nothing is heard of while it is open");
    s.stage = Stage::Done;
    Require(PublicDeed(s) == "{owner} asked {player} to bring {them} {item}, and {player} did it.", "a deed done");
    s.stage = Stage::Declined;
    Require(PublicDeed(s).find("refused") != std::string::npos, "a request turned down");
    s.stage = Stage::Ignored;
    Require(PublicDeed(s).find("never did") != std::string::npos, "a request left undone");
    s.request = Request::Rescue;
    s.other = 7;
    s.stage = Stage::Failed;
    Require(PublicDeed(s).find("{other} is dead") != std::string::npos, "a rescue that came too late");
    Require(PublicDeed(s).find(" I ") == std::string::npos, "third person, never the asker's own words");
}

static void Branches() {
    using A = Book::Amendment;
    Book book;
    // Calling off: only while open, nobody's fault, no follow-up, no deed, remembered with the reason.
    const auto find = book.Open(Ask(1, Request::FindPerson, 50), 10.0f);
    Require(book.CallOff(find, "  she came home\n on her own. ", 10.2f), "a request can be called off");
    const auto& off = At(book, find, "called off");
    Require(off.stage == Stage::Withdrawn && !IsOpen(off) && !IsUnhappy(off), "called off: ended, not unhappy");
    Require(off.note == "she came home on her own", "the reason is one clean line");
    Require(OwnerMemory(off) == "I asked {player} to find {other}, but then I called it off: she came home on her own.",
            "remembered with the reason");
    Require(PublicDeed(off).empty(), "nobody hears of a request called off");
    Require(!book.Unresolved(1), "nothing left to follow up");
    Require(!book.CallOff(find, "again", 10.3f), "an ended one cannot be called off again");
    Require(book.Amend(find, A{Change::MoreTime}, 10.3f) == "nothing is open", "an ended one cannot change");

    // More time and hurry.
    const auto item = book.Open(Ask(2, Request::BringItem), 10.0f);
    book.Find(item)->item = 7;
    book.Find(item)->deadline = 13.0f;
    A more{Change::MoreTime};
    more.note = "none";
    Require(book.Amend(item, more, 11.0f).empty() && At(book, item, "more").deadline == 15.0f, "two more days");
    Require(At(book, item, "more").note.empty(), "\"none\" is no reason");
    Require(book.Amend(item, more, 11.0f).empty() && At(book, item, "more").deadline == 17.0f, "and two more");
    Require(book.Amend(item, more, 11.0f).empty() && At(book, item, "more").deadline == 18.0f, "at most a week from now");
    Require(!book.Amend(item, more, 11.0f).empty(), "no more than that");
    Require(book.Amend(item, A{Change::Hurry}, 11.0f).empty(), "it can become urgent");
    Require(At(book, item, "hurry").deadline == 11.5f, "half a day from now");
    Require((At(book, item, "hurry").changes & Changed::Hurried) && !(At(book, item, "hurry").changes & Changed::MoreTime),
            "urgent, not more time");
    Require(!book.Amend(item, A{Change::Hurry}, 11.1f).empty(), "not more urgent than that");

    // The reward: what was offered stays, more is at most three times as much, less is refused.
    auto& s = *book.Find(item);
    s.promised = 120;
    Require(OfferedGold(s, 30) == 120, "the promised sum is the offer");
    A raise{Change::RaiseReward};
    raise.offered = 120;
    raise.gold = 100;
    Require(book.Amend(item, raise, 11.0f).find("120 gold") != std::string::npos, "less is refused, with the sum");
    raise.gold = 200;
    Require(book.Amend(item, raise, 11.0f).empty() && OfferedGold(At(book, item, "raise"), 30) == 200, "raised to the sum named");
    raise.offered = 200;
    raise.gold = 5000;
    Require(book.Amend(item, raise, 11.0f).empty() && OfferedGold(At(book, item, "raise"), 30) == 600, "at most three times");
    Require(RewardGold(At(book, item, "raise"), 30) == 0, "no gold before it is done");

    // Another item: the objective stays true, the old one is remembered.
    A other{Change::NewSubject};
    other.item = 7;
    Require(!book.Amend(item, other, 11.0f).empty(), "the same item is no change");
    other.item = 9;
    other.note = "the inn ran out";
    Require(book.Amend(item, other, 11.0f).empty(), "another item");
    const auto& changed = At(book, item, "another item");
    Require(changed.item == 9 && changed.before == 7 && changed.note == "the inn ran out", "the new item, the old one kept");
    Require(changed.deadline >= 12.0f, "at least a day for something new");
    Require(ObjectiveFor(changed) == Objective::Recover, "still the same objective");
    book.Apply(item, Event::Achieved, 11.5f);
    Require(!book.Amend(item, other, 11.6f).empty(), "no new subject once the deed is done");
    Require(!book.CallOff(item, "", 11.6f), "nor calling it off");
    book.Apply(item, Event::Met, 11.7f);
    const auto& done = At(book, item, "done");
    Require(OwnerMemory(done) == "I asked {player} to bring me {item} (at first it was {before}), and {player} did it.",
            "remembered with what it was at first");
    Require(RewardGold(done, 30) == 600, "the promised gold is paid");

    // Another person; a threat cannot change.
    const auto word = book.Open(Ask(3, Request::DeliverWord, 60), 10.0f);
    other = A{Change::NewSubject};
    other.other = 3;
    Require(!book.Amend(word, other, 10.5f).empty(), "not the asker themselves");
    other.other = 61;
    other.place = 900;
    Require(book.Amend(word, other, 10.5f).empty(), "another person to speak with");
    Require(At(book, word, "word").other == 61 && At(book, word, "word").before == 60 && At(book, word, "word").place == 900,
            "the new person and where they are");
    const auto threat = book.Open(Ask(4, Request::DealWithThreat, 70), 10.0f);
    other.other = 71;
    Require(!book.Amend(threat, other, 10.5f).empty(), "a threat does not change in talk");

    // An answer sent back: only by the one it is about; it moves the request on; both remember it.
    Require(book.AboutId(61) == word && book.AboutId(60) == 0, "the request is about the new person");
    Require(!book.Reply(word, 60, "hello", false, 10.6f).empty(), "not by someone else");
    Require(!book.Reply(word, 61, "none", false, 10.6f).empty(), "no answer, not going along: nothing");
    Require(book.Reply(word, 61, "Tell him I will think about it.", true, 10.6f).empty(), "an answer and going along");
    const auto& replied = At(book, word, "replied");
    Require(replied.stage == Stage::ReturnTo && ObjectiveFor(replied) == Objective::ReturnTo, "go back with it");
    Require(replied.reply == "Tell him I will think about it." && (replied.changes & Changed::ComesAlong), "recorded");
    Require(OtherMemory(replied) ==
                "{owner} had asked {player} to speak with me. I gave {player} my answer for {owner}: \"Tell him I will think "
                "about it.\" I went along with {player} to see {owner}.",
            "the one it was about remembers");
    book.Apply(word, Event::Met, 11.0f);
    const auto memory = OwnerMemory(At(book, word, "word done"));
    Require(memory.find("{player} brought me {other}'s answer: \"Tell him I will think about it.\"") != std::string::npos &&
                memory.find("{other} came along with {player} to see me.") != std::string::npos,
            "the asker remembers the answer and the visit");
    const auto item2 = book.Open(Ask(5, Request::BringItem), 10.0f);
    Require(!book.Reply(item2, 0, "x", false, 10.0f).empty(), "nobody answers for an item");

    // A long reason is cut at a word.
    const auto esc = book.Open(Ask(6, Request::EscortMe), 10.0f);
    book.Find(esc)->target = 500;
    std::string longText;
    for (int i = 0; i < 60; ++i) longText += "word ";
    Require(book.CallOff(esc, longText, 10.1f) && At(book, esc, "long").note.size() <= kMaxNote &&
                At(book, esc, "long").note.back() != ' ',
            "a long reason is cut at a word");
}

static void SaveDataV2() {
    Book book;
    const auto r = book.Open(Ask(2, Request::DeliverWord, 9), 10.0f);
    auto& s = *book.Find(r);
    s.promised = 250;
    s.changes = Changed::Raised | Changed::ComesAlong;
    s.before = 8;
    s.note = "he moved to Riverwood";
    s.reply = "Tell her \"yes\".";
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "a version 2 round trip");
    const auto* b = back.Find(r);
    Require(b && b->promised == 250 && b->changes == s.changes && b->before == 8 && b->note == s.note && b->reply == s.reply,
            "the branch fields come back");
    // A version 1 record (the same storyline without the branch fields) still loads.
    const std::size_t header = 11, v1 = 42, extra = 4 + 1 + 4 + 2 + s.note.size() + 2 + s.reply.size();
    Require(bytes.size() == header + v1 + extra, "the record is the size it should be");
    std::vector<std::uint8_t> old(bytes.begin(), bytes.begin() + header + v1);
    Book older;
    Require(Deserialize(old, older, 1), "a version 1 record loads");
    Require(older.Find(r) && older.Find(r)->promised == 0 && older.Find(r)->reply.empty(), "with no branch fields");
    Require(!Deserialize(old, older, 2), "but not as version 2");
    Require(!Deserialize(bytes, older, 3), "an unknown version is refused");
}

int main() {
    try {
        Deeds();
        Branches();
        SaveDataV2();
        Meetings();
        Requests();
        Variety();
        SaveData();
    } catch (const std::exception& e) {
        std::cerr << "StorylinesCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "StorylinesCheck passed\n";
    return 0;
}
