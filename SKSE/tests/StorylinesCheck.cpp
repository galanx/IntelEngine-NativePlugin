// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/StorylinesCheck.cpp
#include "Storylines.h"

#include <iostream>
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

int main() {
    try {
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
