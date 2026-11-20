// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/RumorsCheck.cpp
#include "Rumors.h"

#include <iostream>
#include <stdexcept>

using namespace IntelEngine::Rumors;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Rumor Of(Kind kind, std::uint8_t home, float weight, const char* text) {
    Rumor r;
    r.kind = kind;
    r.home = home;
    r.weight = weight;
    r.text = text;
    return r;
}

static int Holds(const Rumor& r) {
    int n = 0;
    for (std::size_t h = 0; h < kHolds; ++h) if (r.heardIn & Bit(static_cast<std::uint8_t>(h))) ++n;
    return n;
}

int main() {
    try {
        // ---- the land
        Require(HoldIndex(0x00016772) == Whiterun && HoldIndex(0x0001676C) == Rift && HoldIndex(0x12345) == NoHold, "the holds by their records");
        for (std::size_t a = 0; a < kHolds; ++a) {
            Require(!(kBorders[a] & Bit(static_cast<std::uint8_t>(a))), "no hold borders itself");
            for (std::size_t b = 0; b < kHolds; ++b) {
                const bool ab = (kBorders[a] & Bit(static_cast<std::uint8_t>(b))) != 0, ba = (kBorders[b] & Bit(static_cast<std::uint8_t>(a))) != 0;
                Require(ab == ba, "a border has two sides");
            }
        }
        Require((kBorders[Haafingar] & Bit(Rift)) == 0 && (kBorders[Whiterun] & Bit(Eastmarch)) != 0, "Solitude is far from Riften; Whiterun lies between");

        // ---- a rumor begins at home
        Ledger l;
        const auto small = l.Add(Of(Kind::Deed, Whiterun, 0.3f, "Ysolda got her mother's ring back."), 10.0f);
        const auto big = l.Add(Of(Kind::Deed, Whiterun, 0.8f, "It was a vampire that killed the hunter by Broken Fang Cave, and it is dead."), 10.0f);
        Require(small == 1 && big == 2 && l.Find(big)->heardIn == Bit(Whiterun) && l.Find(big)->heardAt[Whiterun] == 10.0f, "known where it began, from that day");
        Require(l.Add(Of(Kind::Deed, NoHold, 0.5f, "nowhere"), 10.0f) == 0 && l.Add(Of(Kind::Deed, Whiterun, 0.5f, ""), 10.0f) == 0, "a rumor has a home and something to say");
        Require(l.ForHold(Whiterun, 0, 10.0f, 8).size() == 2 && l.ForHold(Rift, 0, 10.0f, 8).empty(), "told at home, not yet elsewhere");

        // ---- it travels, a hold a day at most, along the borders, and only what carries
        Require(l.Tick(10.5f).empty(), "nothing leaves a hold on the day it began");
        int reached = 0;
        for (float day = 11.0f; day < 16.0f; day += 1.0f) {
            for (const auto& e : l.Tick(day)) {
                Require(e.type == Event::Reached && e.rumor.id == big, "only what carries travels; nothing fades this soon");
                bool neighbour = false;
                for (std::size_t h = 0; h < kHolds; ++h) {
                    // It came from a hold that had it for a day already and borders the new one.
                    if ((e.rumor.heardIn & Bit(static_cast<std::uint8_t>(h))) && h != e.hold && (kBorders[h] & Bit(e.hold)) && day - e.rumor.heardAt[h] >= kTravelDays) neighbour = true;
                }
                Require(neighbour, "along a border, from a hold that has had it a day");
                ++reached;
            }
        }
        Require(Holds(*l.Find(small)) == 1, "a small thing stays at home");
        Require(reached >= 1 && reached <= 3 && Holds(*l.Find(big)) == reached + 1, "a bigger one reaches a few holds, never more than it carries");
        {
            // The same save tells it the same way.
            Ledger again;
            again.Add(Of(Kind::Deed, Whiterun, 0.3f, "Ysolda got her mother's ring back."), 10.0f);
            again.Add(Of(Kind::Deed, Whiterun, 0.8f, "It was a vampire that killed the hunter by Broken Fang Cave, and it is dead."), 10.0f);
            again.Tick(10.5f);
            for (float day = 11.0f; day < 16.0f; day += 1.0f) again.Tick(day);
            Require(again.Find(big)->heardIn == l.Find(big)->heardIn, "the same rumor travels the same roads every time");
        }
        {
            Ledger wide;
            const auto war = wide.Add(Of(Kind::News, Eastmarch, 1.0f, "Windhelm and Riften are at each other's throats."), 0.0f);
            for (float day = 1.0f; day < 9.0f; day += 1.0f) wide.Tick(day);
            Require(Holds(*wide.Find(war)) >= 5, "great news goes round the land");
        }

        // ---- it is forgotten
        {
            Ledger old;
            const auto deed = old.Add(Of(Kind::Deed, Rift, 0.3f, "Someone paid the fishmonger what he was owed."), 0.0f);
            const float life = LifeDays(*old.Find(deed));
            Require(old.Tick(life - 0.1f).empty() && old.Find(deed), "still talked of");
            const auto gone = old.Tick(life + 0.1f);
            Require(gone.size() == 1 && gone.front().type == Event::Forgotten && gone.front().rumor.id == deed && !old.Find(deed), "and then no more");
        }

        // ---- what is told to the player, and by whom
        Ledger t;
        const auto deed = t.Add(Of(Kind::Deed, Whiterun, 0.7f, "The brothers at the Drunken Huntsman have fallen out."), 5.0f);
        auto plea = Of(Kind::Trouble, Whiterun, 0.6f, "Anoriath is asking for someone who will go and look at the dead hunter by Graywinter Watch.");
        plea.lead = 0x1A680;
        plea.owner = Owner::Arc;
        plea.ownerId = 7;
        const auto trouble = t.Add(plea, 5.5f);
        auto about = Of(Kind::Deed, Whiterun, 0.9f, "Nazeem sold hides that were not his.");
        about.about = 0x1A6A4;
        const auto shame = t.Add(about, 5.6f);
        const auto first = t.ForHold(Whiterun, 0x1A66E, 6.0f, 8);
        Require(first.size() == 3 && first.front()->id == trouble, "trouble is told before anything else");
        Require(t.ForHold(Whiterun, 0x1A680, 6.0f, 8).size() == 2, "the one who wants the help does not gossip about it");
        Require(t.ForHold(Whiterun, 0x1A6A4, 6.0f, 8).size() == 2, "nobody passes on what is said about themselves");
        Require(t.ForHold(Whiterun, 0, 6.0f, 1).size() == 1, "no more than was asked for");
        Require(t.ToBringUp(Whiterun, 0x1A66E, 6.0f) && t.ToBringUp(Whiterun, 0x1A66E, 6.0f)->id == trouble, "a publican brings up the trouble first");
        t.Told(trouble, 6.0f);
        Require(t.Find(trouble)->told == 1 && t.ToBringUp(Whiterun, 0x1A66E, 6.1f) && t.ToBringUp(Whiterun, 0x1A66E, 6.1f)->id == shame, "told once, the next thing");
        t.Told(shame, 6.1f);
        t.Told(deed, 6.2f);
        Require(!t.ToBringUp(Whiterun, 0x1A66E, 6.3f), "nothing is brought up twice");
        Require(t.ForHold(Whiterun, 0x1A66E, 6.3f, 8).size() == 3, "asked, they still know it all");
        {
            Ledger stale;
            stale.Add(Of(Kind::Deed, Whiterun, 0.7f, "Old news."), 0.0f);
            Require(!stale.ToBringUp(Whiterun, 0, 4.0f), "old news is not brought up unasked");
            stale.Add(Of(Kind::Deed, Whiterun, 0.2f, "A small thing."), 4.0f);
            Require(!stale.ToBringUp(Whiterun, 0, 4.1f), "nor a small thing");
        }

        // ---- withdrawn with what it belongs to
        const auto gone = t.Withdraw(Owner::Arc, 7);
        Require(gone.size() == 1 && gone.front().id == trouble && !t.Find(trouble) && t.all.size() == 2, "a story given up takes its rumor with it");
        Require(t.Withdraw(Owner::Arc, 7).empty(), "once");

        // ---- a full ledger lets the least go, never the trouble
        {
            Ledger full;
            auto keep = Of(Kind::Trouble, Reach, 0.2f, "Someone at the mine wants a word with whoever will listen.");
            const auto kept = full.Add(keep, 0.0f);
            for (std::size_t i = 0; i < kMaxRumors + 5; ++i) {
                std::vector<Rumor> dropped;
                full.Add(Of(Kind::Deed, Reach, 0.5f, "Something happened."), 1.0f + static_cast<float>(i) * 0.01f, &dropped);
                Require(dropped.size() <= 1, "one goes for one that comes");
                for (const auto& d : dropped) Require(d.id != kept, "the trouble stays");
            }
            Require(full.all.size() == kMaxRumors && full.Find(kept), "never more than the ledger holds");
        }

        // ---- the record
        l.Find(big)->entry[Whiterun] = 4711;
        l.Told(big, 12.0f);
        const auto bytes = Serialize(l);
        Ledger back;
        Require(Deserialize(bytes, back), "the ledger survives the co-save");
        Require(back.all.size() == l.all.size() && back.nextId == l.nextId && back.Find(big)->text == l.Find(big)->text && back.Find(big)->heardIn == l.Find(big)->heardIn &&
                back.Find(big)->entry[Whiterun] == 4711 && back.Find(big)->told == 1 && back.Find(big)->heardAt == l.Find(big)->heardAt, "whole");
        auto cut = bytes;
        cut.pop_back();
        Ledger none;
        Require(!Deserialize(cut, none) && none.all.empty(), "a cut record is refused whole");
        auto more = bytes;
        more.push_back(0);
        Require(!Deserialize(more, none), "and one with something after it");
        Require(!Deserialize(bytes, none, 2), "an unknown version is refused");
        {
            Ledger bad;
            bad.Add(Of(Kind::Deed, Whiterun, 0.5f, "x"), 1.0f);
            auto raw = Serialize(bad);
            raw[4 + 2 + 4] = 9;   // no such kind (after nextId, the count and the id)
            Require(!Deserialize(raw, none), "a kind that does not exist is refused");
        }

        std::cout << "RumorsCheck passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RumorsCheck FAILED: " << error.what() << "\n";
        return 1;
    }
}
