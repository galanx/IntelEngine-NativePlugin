// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src /I <vcpkg include> SKSE/tests/TalesCheck.cpp
#include "Tales.h"

#include <iostream>
#include <stdexcept>
#include <string>

using namespace IntelEngine;
using namespace IntelEngine::Arcs;
using nlohmann::json;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Tales::Pool Town() {
    Tales::Pool pool;
    pool.people = {
        {"p0", "Ysolda", 0x100, true, false, false},
        {"p1", "Brenuin", 0x101, true, true, false},
        {"p2", "Hulda", 0x102, true, false, false},
        {"p3", "Uthgerd", 0x103, false, true, true},
    };
    pool.items = {{"Silver Ring", 0x500}, {"Iron Dagger", 0x501}};
    pool.sites = {{"d1", "Mzinchaleft", 0x700, 0x701, Tales::SiteDwemer},
                  {"d2", "Embershard Mine", 0x710, 0x711, Tales::SiteBandits},
                  {"d3", "Bleakwind Basin", 0x720, 0x721, Tales::SiteGiants}};
    pool.gold = 150;
    return pool;
}

// A tale with every kind of step the author may use.
static json Sample() {
    return json::parse(R"({
      "title": "The Ring and the Debt",
      "genre": "crime",
      "premise": "Brenuin took Ysolda's ring to pay a debt to Uthgerd.",
      "people": [
        {"who": "p1", "part": "the thief", "truth": "I took Ysolda's ring. I owe Uthgerd."},
        {"who": "p0", "part": "the one who asks", "truth": "My mother's ring is gone."},
        {"who": "p3", "part": "the lender", "truth": "Brenuin owes me fifty gold."}
      ],
      "items": [ {"id": "i1", "item": "silver ring", "name": "Ysolda's Mother's Ring", "hidden": "home", "of": "p1"} ],
      "offer": {"words": "Ysolda comes to {player}, upset about her mother's ring.", "ask": "Find my mother's ring.",
                "journal": "Ysolda asked me to find her mother's ring.", "reward": 5000, "days": 9},
      "steps": [
        {"id": "s1", "do": "talk", "with": "p1", "goal": "Ask Brenuin about the ring",
         "outcomes": [
           {"if": "Brenuin admits he took it to pay Uthgerd", "then": "s3", "journal": "Brenuin admitted it."},
           {"if": "Brenuin denies it", "then": "s2", "journal": "Brenuin denied everything."}]},
        {"id": "s2", "do": "find", "item": "i1", "goal": "Search Brenuin's home for the ring", "then": "s4", "journal": "I found it."},
        {"id": "s3", "do": "choose", "with": "p3", "goal": "Settle Brenuin's debt with Uthgerd", "question": "What now?",
         "options": [
           {"say": "Talk Uthgerd into forgiving the debt", "persuade": true, "then": "s2", "journal": "Uthgerd let it go.",
            "effect": "befriend p1 p3", "else": "stay", "else_journal": "Uthgerd would not hear of it."},
           {"say": "Pay Uthgerd the fifty gold", "then": "s2", "journal": "I paid Uthgerd.", "effect": "player pays p3 50"},
           {"say": "Fight Uthgerd for it", "then": "s5", "journal": "I challenged Uthgerd."}]},
        {"id": "s4", "do": "give", "item": "i1", "to": "p0", "goal": "Give the ring back to Ysolda", "then": "end:good", "journal": "Ysolda has it back."},
        {"id": "s5", "do": "brawl", "with": "p3", "goal": "Beat Uthgerd",
         "won": {"then": "s2", "journal": "I beat Uthgerd."}, "lost": {"then": "end:bad", "journal": "Uthgerd beat me."}},
        {"id": "s9", "do": "wait", "hours": 5, "goal": "never reached", "then": "end:good"}
      ],
      "endings": {
        "good": {"journal": "The ring is home.", "rumor": "Ysolda has her ring back.", "memories": {"p0": "{player} brought my ring back."}},
        "bad": {"journal": "The ring is lost.", "rumor": "", "pay": false}
      }
    })");
}

static void CompilesAndRuns() {
    auto result = Tales::Compile(Sample(), Town());
    if (!result.ok) std::cerr << result.errors;
    Require(result.ok, "the sample tale compiles");
    Require(result.notes.find("s9") != std::string::npos, "an unreachable step is left out, with a note");
    Arc a = result.arc;
    Require(a.graph.size() == 5, "five steps kept");
    Require(a.cast[RAsker] == 0x100 && a.cast[2] == 0x101 && a.cast[3] == 0x103, "the asker is role 1, the rest in order");
    Require(a.gold == 1000 && a.days == 7.0f, "reward and days are kept within limits");
    Require(a.propBase[0] == 0x500 && a.propHome[0] == 2, "the ring hides at Brenuin's home");
    Require(a.graph[0].objective == kTaleObjective && a.graph[4].objective == kTaleObjective + 4, "objectives follow the steps");
    Require(a.graph[0].edges[0].when == If::Dead && a.graph[0].edges[0].ending == Ending::Lost, "a needed person dead ends it");
    Require(a.price == 50, "the price the player may pay");
    Require(a.story == "Ysolda asked me to find her mother's ring.", "the journal opens with the offer's line");

    Book book;
    const auto id = book.Plan(a, 1.0f);
    Require(id != 0 && book.Accept(id, 1.0f), "a tale is planned and taken on");
    Arc& t = *book.Find(id);
    Require(t.deadline == 8.0f, "a tale stands its own days");
    // Talk: nothing before the judge says which way it went.
    Facts talk;
    talk.with[2] = talk.spoke[2] = true;
    Require(!Advance(t, talk, 1.1f).any(), "a talk waits for the judge");
    talk.turn = 0;
    auto c = Advance(t, talk, 1.2f);
    Require(t.node == 2 && c.line == "Brenuin admitted it.", "outcome 0 leads to the debt choice");
    Require(t.heard[2].find("Brenuin admits he took it") != std::string::npos, "Brenuin remembers what came out");
    // Persuade fails with low Speech: stays, with its own line.
    Facts choose;
    choose.answer = 0;
    choose.speech = 10;
    c = Advance(t, choose, 1.3f);
    Require(t.node == 2 && c.stayed && c.line == "Uthgerd would not hear of it.", "not persuasive: the same step again");
    choose.speech = 80;
    c = Advance(t, choose, 1.4f);
    Require(t.node == 1 && c.effect == Effect::Befriend && c.effectRole == 2 && c.bondWith == 3, "persuaded: friends, on to the search");
    Facts found;
    found.has[0] = true;
    c = Advance(t, found, 1.5f);
    Require(t.node == 3, "found: give it back");
    Facts handed;
    handed.handed[0] = true;
    c = Advance(t, handed, 1.6f);
    Require(t.phase == Phase::Ended && t.ending == Ending::Done && c.effect == Effect::PayGold, "given back: the good ending, paid");
    Require(TaleClosing(t) == "The ring is home." && TaleRumor(t) == "Ysolda has her ring back.", "the author's ending words");
    Require(TaleEndMemory(t, RAsker) == "{player} brought my ring back.", "the asker's memory");
    Require(t.story.find("Ysolda has it back.") != std::string::npos, "the story so far grows");

    // Save and load keep a tale whole.
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "a tale survives the co-save");
    const Arc& b = *back.Find(id);
    Require(b.graph.size() == 5 && b.graph[2].options[0] == t.graph[2].options[0] && b.ends[0].memory[RAsker] == t.ends[0].memory[RAsker] &&
                b.story == t.story && b.price == 50 && b.graph[0].turns[1] == "Brenuin denies it",
            "everything of the tale comes back");
    Require(Serialize(back) == bytes, "and saves the same again");
}

static void BrawlAndWaitAndDeath() {
    auto result = Tales::Compile(Sample(), Town());
    Book book;
    const auto id = book.Plan(result.arc, 1.0f);
    book.Accept(id, 1.0f);
    Arc& t = *book.Find(id);
    Facts f;
    f.turn = 0;
    Advance(t, f, 1.1f);
    Facts fight;
    fight.answer = 2;
    Advance(t, fight, 1.2f);
    Require(t.graph[t.node].kind == Kind::Brawl, "the fight");
    Facts lost;
    lost.brawl = -1;
    auto c = Advance(t, lost, 1.3f);
    Require(t.ending == Ending::Failed && c.effect == Effect::None, "lost: the bad ending, unpaid");
    Require(TaleClosing(t) == "The ring is lost.", "the bad ending's words");

    // Brenuin dead before the talk: lost.
    const auto id2 = book.Plan(result.arc, 2.0f);
    book.Accept(id2, 2.0f);
    Arc& u = *book.Find(id2);
    Facts dead;
    dead.dead[2] = true;
    c = Advance(u, dead, 2.1f);
    Require(u.ending == Ending::Lost && c.line == "{2} is dead.", "a needed person dead: lost, with its line");
    Require(EndStage(Pattern::Tale, Ending::Lost) == kTaleBadStage && EndStage(Pattern::Tale, Ending::Done) == kTaleGoodStage, "journal stages");

    // A wait step.
    json w = Sample();
    w["steps"] = json::parse(R"([{"id": "a", "do": "wait", "hours": 6, "goal": "Give Brenuin a few hours", "then": "b"},
                                  {"id": "b", "do": "visit", "with": "p1", "goal": "Brenuin comes to you", "then": "end:good", "journal": "He came."}])");
    auto wr = Tales::Compile(w, Town());
    Require(wr.ok, "wait and visit compile");
    const auto id3 = book.Plan(wr.arc, 3.0f);
    book.Accept(id3, 3.0f);
    Arc& v = *book.Find(id3);
    Require(!Advance(v, Facts{}, 3.2f).any(), "not yet six hours");
    Advance(v, Facts{}, 3.26f);
    Require(v.node == 1 && v.graph[1].kind == Kind::Visit, "six hours on: the visit");
    Facts came;
    came.with[2] = came.spoke[2] = true;
    Advance(v, came, 3.3f);
    Require(v.phase == Phase::Open && v.node == 2 && IsWayBack(v, v.graph[2]), "he came and talked: back to Ysolda for the reward");
}

static void PaidInPerson() {
    // The reward changes hands in person. A tale that ends away from its asker (here: Brenuin comes and owns up) gets
    // one step more: back to Ysolda, who hears how it ended and pays ("Faendal gave you 60 gold" with Faendal four
    // thousand units away, test game 2026-11-19).
    json w = Sample();
    w["steps"] = json::parse(R"([{"id": "a", "do": "wait", "hours": 6, "goal": "Give Brenuin a few hours", "then": "b"},
                                  {"id": "b", "do": "visit", "with": "p1", "goal": "Brenuin comes to you", "then": "end:good", "journal": "He came."}])");
    const auto r = Tales::Compile(w, Town());
    Require(r.ok && r.arc.graph.size() == 3, "one step more than the author wrote");
    Require(r.notes.find("paid in person") != std::string::npos, "and a note says so");
    const Node& back = r.arc.graph[2];
    Require(back.kind == Kind::Talk && back.who == RAsker && back.marker == RAsker && back.objective == kTaleObjective + 2 &&
                back.goal == "Tell {1} how it ended" && back.turns[0].empty(),
            "the way back: a talk with the asker, marked in the journal, nothing for the judge");
    Require(r.arc.graph[1].edges[1].when == If::Done && r.arc.graph[1].edges[1].next == 2 && r.arc.graph[1].edges[1].ending == Ending::None &&
                r.arc.graph[1].edges[1].effect == Effect::None && r.arc.graph[1].edges[1].line == "He came.",
            "the visit leads there with its own journal line, and pays nothing yet");
    const auto atTheWayBack = [&](Book& book) -> Arc& {
        const auto id = book.Plan(r.arc, 3.0f);
        book.Accept(id, 3.0f);
        Arc& t = *book.Find(id);
        Advance(t, Facts{}, 3.26f);
        Facts came;
        came.with[2] = came.spoke[2] = true;
        const auto c = Advance(t, came, 3.3f);
        Require(t.phase == Phase::Open && t.node == 2 && c.effect == Effect::None && c.ending == Ending::None, "on the way back, unpaid");
        return t;
    };
    {
        Book book;
        Arc& t = atTheWayBack(book);
        Require(t.deadline >= 3.3f + 4.0f, "the way back keeps its own days");
        Facts near;
        near.with[RAsker] = true;
        Require(!Advance(t, near, 3.4f).any(), "standing by the asker is not yet telling them");
        near.spoke[RAsker] = true;
        const auto c = Advance(t, near, 3.5f);
        Require(t.phase == Phase::Ended && t.ending == Ending::Done && c.effect == Effect::PayGold && c.effectRole == RAsker, "told: the good ending, paid by the asker");
        Require(t.heard[RAsker].find("came back and told me") != std::string::npos, "the asker remembers being told");
        Require(TaleClosing(t) == "The ring is home.", "the ending is the one the story had reached");
        // Through the co-save.
        const auto bytes = Serialize(book);
        Book again;
        Require(Deserialize(bytes, again) && Serialize(again) == bytes, "a way back survives the co-save");
    }
    {
        // Never gone back: after three days the story ends as it had, unpaid.
        Book book;
        Arc& t = atTheWayBack(book);
        Require(!Advance(t, Facts{}, 3.3f + 2.9f).any(), "two days on: still waiting");
        const auto c = Advance(t, Facts{}, 3.3f + 3.05f);
        Require(t.phase == Phase::Ended && t.ending == Ending::Done && c.effect == Effect::None &&
                    c.line.find("never went back") != std::string::npos,
                "never gone back: the same ending, no gold");
    }
    {
        // The asker dies before hearing of it: the same ending, no gold, and not "the asker died" (the story was over).
        Book book;
        Arc& t = atTheWayBack(book);
        Facts dead;
        dead.dead[RAsker] = true;
        const auto c = Advance(t, dead, 3.4f);
        Require(t.phase == Phase::Ended && t.ending == Ending::Done && c.effect == Effect::None && c.line.find("died before") != std::string::npos,
                "the asker dead on the way back: the story's own ending, unpaid");
    }
    // A story that ends with the asker needs no way back (the sample: the ring is given to Ysolda herself).
    Require(Tales::Compile(Sample(), Town()).arc.graph.size() == 5, "ending with the asker: nothing added");
    // "pay" written on a branch to an ending that does not pay by itself: the same way back, for that ending.
    json bad = Sample();
    bad["steps"][4]["lost"]["effect"] = "pay";
    const auto rb = Tales::Compile(bad, Town());
    Require(rb.ok && rb.arc.graph.size() == 6 && IsWayBack(rb.arc, rb.arc.graph[5]) && rb.arc.graph[5].edges[1].ending == Ending::Failed &&
                rb.arc.graph[5].edges[1].effect == Effect::PayGold,
            "a paid bad ending away from the asker goes by the asker too");
    bool direct = false;
    for (const auto& e : rb.arc.graph[4].edges) if (e.when == If::Lost) direct = e.next == 5 && e.effect == Effect::None && e.ending == Ending::None;
    Require(direct, "the brawl lost leads to the way back, unpaid until then");
    // In the middle of a story the asker cannot pay from afar either.
    json mid = Sample();
    mid["steps"][1]["effect"] = "pay";   // found in Brenuin's home: Ysolda is not there
    Require(Tales::Compile(mid, Town()).errors.find("they pay in person") != std::string::npos, "no gold from afar in the middle of a story");
    mid = Sample();
    mid["steps"][3]["effect"] = "pay";   // the ring given to Ysolda herself
    Require(Tales::Compile(mid, Town()).ok, "paid on a step with the asker is fine");
    // Twelve steps and a way back do not fit: the author is told how many do.
    json longTale = Sample();
    json steps = json::array();
    for (int i = 0; i < 12; ++i) {
        json s = json::parse(R"({"do": "wait", "hours": 1, "goal": "Wait"})");
        s["id"] = "w" + std::to_string(i);
        s["then"] = i == 11 ? std::string("end:good") : "w" + std::to_string(i + 1);
        steps.push_back(s);
    }
    longTale["steps"] = steps;
    longTale["items"] = json::array();
    Require(Tales::Compile(longTale, Town()).errors.find("at most 11 here") != std::string::npos, "the way back needs a step of its own");
    steps.erase(steps.begin());
    longTale["steps"] = steps;
    Require(Tales::Compile(longTale, Town()).ok, "eleven steps and the way back fit");
}

static void CloserAndApart() {
    // A story between Ysolda and the player: where they come closer the bond grows a step, where the player is
    // unkind it cools. The effects name a person; the engine does the rest (Hearts.h RankAfter, in the script).
    json j = Sample();
    j["items"] = json::array();
    j["steps"] = json::parse(R"([
      {"id": "a", "do": "choose", "with": "p0", "goal": "Answer Ysolda", "question": "What do you tell her?",
       "options": [{"say": "Walk with her to the gate at dusk", "then": "end:good", "journal": "We walked.", "effect": "closer p0"},
                   {"say": "Laugh it off", "then": "end:bad", "journal": "I laughed.", "effect": "apart Ysolda"}]}])");
    const auto r = Tales::Compile(j, Town());
    if (!r.ok) std::cerr << r.errors;
    Require(r.ok, "closer and apart compile");
    const auto& n = r.arc.graph[0];
    // The good ending pays (with the asker at hand), so the reward is the second effect.
    Require(n.edges[0].effect == Effect::Closer && n.edges[0].effectRole == RAsker && n.edges[0].effect2 == Effect::PayGold, "closer, then the reward");
    Require(n.edges[1].effect == Effect::Apart && n.edges[1].effectRole == RAsker && n.edges[1].effect2 == Effect::None, "apart, by name");
    Book book;
    const auto id = book.Plan(r.arc, 1.0f);
    book.Accept(id, 1.0f);
    Facts f;
    f.answer = 1;
    const auto c = Advance(*book.Find(id), f, 1.1f);
    Require(c.effect == Effect::Apart && c.effectRole == RAsker && c.ending == Ending::Failed, "the unkind answer cools it");
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back) && back.Find(id)->graph[0].edges[0].effect == Effect::Closer, "the new effects survive the co-save");
    // A story between the two of them is not paid, whatever the author wrote, and the offer names no reward.
    auto hearts = Town();
    hearts.heart = true;
    const auto h = Tales::Compile(j, hearts);
    Require(h.ok && h.arc.gold == 0 && !h.arc.ends[0].pay, "a story of the heart promises no gold");
    Require(h.arc.graph[0].edges[0].effect == Effect::Closer && h.arc.graph[0].edges[0].effect2 == Effect::None, "and pays none at its ending");
    Require(OfferPrompt(h.arc).find("Reward") == std::string::npos && OfferPrompt(r.arc).find("Reward: {gold} gold.") != std::string::npos,
            "the offer says so");
    j["steps"][0]["options"][0]["effect"] = "closer";
    Require(Tales::Compile(j, Town()).errors.find("missing a person") != std::string::npos, "closer to whom");
    j["steps"][0]["options"][0]["effect"] = "closer p9";
    Require(!Tales::Compile(j, Town()).ok, "only someone of the story");
}

static void HandsOver() {
    // A talk where the person gives the item up: the effect carries the prop and the giver.
    json j = Sample();
    j["steps"][0]["outcomes"][0]["effect"] = "give i1";
    j["steps"][0]["outcomes"][0]["then"] = "s4";
    auto r = Tales::Compile(j, Town());
    Require(r.ok, "a talk that hands the item over compiles");
    Book book;
    const auto id = book.Plan(r.arc, 1.0f);
    book.Accept(id, 1.0f);
    Arc& t = *book.Find(id);
    Facts f;
    f.turn = 0;
    const auto c = Advance(t, f, 1.1f);
    Require(c.effect == Effect::GiveItem && c.effectRole == 2 && c.effectProp == 0, "Brenuin hands the ring over");
    Require(t.graph[t.node].kind == Kind::Give, "then it goes back to Ysolda");
    j["steps"][2]["options"][0]["effect"] = "give i1";
    Require(Tales::Compile(j, Town()).errors.find("cannot hand an item over") != std::string::npos, "not on a Speech option");
    j = Sample();
    j["steps"][0]["outcomes"][0]["effect"] = "give i7";
    Require(Tales::Compile(j, Town()).errors.find("names no item") != std::string::npos, "only the tale's own items");
}

static void Adventure() {
    // Into a Dwemer ruin for what a bandit chief carries off there, then home.
    json j = Sample();
    j["items"] = json::parse(R"([{"id": "i1", "item": "Silver Ring", "name": "Ysolda's Ring", "hidden": "at", "of": "Embershard Mine"}])");
    j["steps"] = json::parse(R"([
      {"id": "a", "do": "delve", "place": "d1", "goal": "Search Mzinchaleft for Brenuin", "then": "b", "journal": "I went deep into Mzinchaleft."},
      {"id": "b", "do": "slay", "place": "d2", "boss": "Grelka the Silent", "goal": "Kill Grelka in Embershard Mine", "then": "c", "journal": "Grelka is dead."},
      {"id": "c", "do": "find", "item": "i1", "goal": "Take the ring from Grelka", "then": "e", "journal": "I have the ring."},
      {"id": "e", "do": "give", "item": "i1", "to": "p0", "goal": "Give Ysolda her ring", "then": "end:good", "journal": "Ysolda has it."}])");
    auto r = Tales::Compile(j, Town());
    if (!r.ok) std::cerr << r.errors;
    Require(r.ok, "an adventure compiles");
    const Arc& a = r.arc;
    // Places take slots in the order they are first named: the ring's (Embershard) before the first step's.
    Require(a.site[0] == 0x710 && a.siteAnchor[0] == 0x711 && a.site[1] == 0x700 && a.siteKind[1] == Tales::SiteDwemer, "places take slots in order");
    Require(a.bossName[0] == "Grelka the Silent" && a.propSite[0] == 1, "the boss is named and carries the ring");
    Require(a.graph[0].kind == Kind::Delve && a.graph[0].site == 2 && a.graph[1].kind == Kind::Slay && a.graph[1].site == 1, "delve and slay");
    Book book;
    const auto id = book.Plan(a, 1.0f);
    book.Accept(id, 1.0f);
    Arc& t = *book.Find(id);
    Facts f;
    Require(!Advance(t, f, 1.1f).any(), "nothing before the depths");
    f.atDepths[1] = true;
    Advance(t, f, 1.2f);
    Require(t.node == 1, "the depths reached");
    Facts dead;
    dead.bossDead[0] = true;
    Advance(t, dead, 1.3f);
    Require(t.node == 2, "the boss dead");
    // Through the co-save.
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "places survive the co-save");
    Require(back.Find(id)->bossName[0] == "Grelka the Silent" && back.Find(id)->graph[1].site == 1 && back.Find(id)->propSite[0] == 1, "whole");
    // A save made before ambushes (record version 4: no ambush block) still loads.
    auto v4 = bytes;
    v4.resize(v4.size() - (3 * 4 + 2 + 1) - 3 - 14 - 12 - 21);   // (and the 21 of version 10: a trail, what an ending leaves open, sequels) the ambush block, the three bytes of someone held (version 7), the fourteen of remains (8), the twelve of what walks by night (9)
    Book older;
    Require(Deserialize(v4, older, 4) && older.Find(id)->bossName[0] == "Grelka the Silent", "a record of version 4 loads");

    j["steps"][0]["place"] = "Blackreach";
    Require(Tales::Compile(j, Town()).errors.find("no place of the list") != std::string::npos, "only the places offered");
    j["steps"][0]["place"] = "d3";
    j["steps"][1]["place"] = "d1";   // a third place: Embershard (the ring), Bleakwind Basin, Mzinchaleft
    Require(Tales::Compile(j, Town()).errors.find("at most 2 places") != std::string::npos, "two places at most");
}

static void Ambush() {
    // Uthgerd's hired men wait on the road; their leader carries the ring; then it goes home.
    json j = Sample();
    j["items"] = json::parse(R"([{"id": "i1", "item": "Silver Ring", "name": "Ysolda's Ring", "hidden": "ambush"}])");
    j["steps"] = json::parse(R"([
      {"id": "a", "do": "ambush", "by": "p3", "why": "keep {player} from asking questions in Riverwood", "leader": "Krev the Knife",
       "goal": "Take the road to Riverwood", "then": "b", "journal": "Hired men waited for me on the road.",
       "lost": {"then": "end:bad", "journal": "They beat me down and took the letter."},
       "outcomes": [{"if": "the leader takes 150 gold from {player} and hands over what Uthgerd gave him", "then": "c",
                     "journal": "I bought them off.", "effect": "give i1"},
                    {"if": "the leader takes gold to walk away", "then": "end:bad", "journal": "I paid them to go.", "effect": "player pays 150"}]},
      {"id": "b", "do": "find", "item": "i1", "goal": "Search the leader's body", "then": "c", "journal": "He had Ysolda's ring."},
      {"id": "c", "do": "give", "item": "i1", "to": "p0", "goal": "Give Ysolda her ring", "then": "end:good", "journal": "Ysolda has it."}])");
    auto r = Tales::Compile(j, Town());
    if (!r.ok) std::cerr << r.errors;
    Require(r.ok, "an ambush compiles");
    const Arc& a = r.arc;
    const auto& n = a.graph[0];
    Require(n.kind == Kind::Ambush && n.who != RNone && a.cast[n.who] == 0x103, "the ambush, sent by Uthgerd");
    Require(n.marker == RNone && n.edges[0].when == If::Lost && n.edges[1].when == If::Done, "no marker on who sent them, and their death changes nothing");
    Require(n.edges[2].when == If::Said0 && n.edges[2].effect == Effect::GiveItem && n.edges[2].next == 2 && !n.turns[0].empty(), "talked out of it: the leader hands it over");
    Require(n.edges[3].when == If::Said1 && n.edges[3].effect == Effect::PlayerPays && n.edges[3].effectRole == RNone && a.price == 150,
            "bought off: the gold goes to the men's leader");
    Require(a.thugName == "Krev the Knife" && a.propAmbush == 1 && a.graph[1].marker == RNone, "the leader is named and carries the ring");
    Book book;
    const auto id = book.Plan(a, 1.0f);
    book.Accept(id, 1.0f);
    Arc& t = *book.Find(id);
    Facts f;
    f.dead[n.who] = true;
    Require(!Advance(t, f, 1.1f).any(), "nothing before the men are down, whoever else died");
    {
        // Beaten: the story goes on (here it ends badly), nobody reloads.
        Book lostBook;
        const auto lostId = lostBook.Plan(a, 1.0f);
        lostBook.Accept(lostId, 1.0f);
        Facts beaten;
        beaten.brawl = -1;
        const auto change = Advance(*lostBook.Find(lostId), beaten, 1.1f);
        Require(change.ending == Ending::Failed && change.line == "They beat me down and took the letter.", "beaten is the story's own turn");
        // Talked out of it: the judge's reading of the talk with the leader.
        Book talkBook;
        const auto talkId = talkBook.Plan(a, 1.0f);
        talkBook.Accept(talkId, 1.0f);
        Facts talked;
        talked.turn = 0;
        const auto settled = Advance(*talkBook.Find(talkId), talked, 1.1f);
        Require(talkBook.Find(talkId)->node == 2 && settled.effect == Effect::GiveItem && settled.effectProp == 0, "talked out of it: on to the hand-over");
    }
    f.ambushOver = true;
    Advance(t, f, 1.2f);
    Require(t.node == 1, "the men are down");
    t.thug = {0xFF000801u, 0xFF000802u, 0u};
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "an ambush survives the co-save");
    const Arc& b = *back.Find(id);
    Require(b.thug[0] == 0xFF000801u && b.thug[1] == 0xFF000802u && b.thugName == "Krev the Knife" && b.propAmbush == 1 && b.graph[0].kind == Kind::Ambush,
            "whole");
    Book untouched;
    Require(!Deserialize(bytes, untouched, 4), "an ambush step read as version 4 is refused, not misread");
    // A save of record version 5 (ambushes, before stalkers) loads as it was.
    Book five;
    auto v5 = bytes;
    v5.resize(v5.size() - 3 - 14 - 12 - 21);   // before someone could be held (version 7), remains examined (8) or anything walked by night (9)
    Require(Deserialize(v5, five, 5) && five.Find(id)->graph[0].kind == Kind::Ambush, "a record of version 5 loads");

    const auto errorsOf = [](json x) { return Tales::Compile(x, Town()).errors; };
    json two = j;
    two["steps"][1] = two["steps"][0];
    two["steps"][1]["id"] = "b";
    two["steps"][1]["then"] = "c";
    Require(errorsOf(two).find("one ambush in a story") != std::string::npos, "one ambush a story");
    json noWhy = j;
    noWhy["steps"][0].erase("why");
    Require(errorsOf(noWhy).find("needs \"why\"") != std::string::npos, "the men know what they were paid for");
    // A beat told lately is not on offer: the compiler refuses it whatever the author wrote.
    auto quiet = Town();
    quiet.barred = 1u << static_cast<unsigned>(Kind::Ambush);
    Require(Tales::Compile(j, quiet).errors.find("no \"ambush\" in this story") != std::string::npos, "an ambush is not told back to back");
    quiet.barred = 1u << static_cast<unsigned>(Kind::Brawl);
    Require(Tales::Compile(j, quiet).ok, "what is not held back is told as written");
    json noLost = j;
    noLost["steps"][0].erase("lost");
    Require(errorsOf(noLost).find("needs \"lost\"") != std::string::npos, "a beaten player has a way on");
    json three = j;
    three["steps"][0]["outcomes"].push_back(three["steps"][0]["outcomes"][0]);
    Require(errorsOf(three).find("at most two outcomes") != std::string::npos, "two outcomes of talk at most");
    json payNobody = j;
    payNobody["steps"][2]["effect"] = "player pays 150";
    Require(!errorsOf(payNobody).empty(), "gold with no receiver only goes to the men's leader");
    json noStep = j;
    noStep["steps"][0] = json::parse(R"({"id": "a", "do": "wait", "hours": 5, "goal": "Wait", "then": "b"})");
    Require(errorsOf(noStep).find("needs a step that is an ambush") != std::string::npos, "an item on the leader needs an ambush");
    json nobody = j;
    nobody["steps"][0].erase("by");
    Require(Tales::Compile(nobody, Town()).ok && Tales::Compile(nobody, Town()).arc.graph[0].who == RNone, "sent by nobody known yet");
}

static void Stalk() {
    // Uthgerd shadows the player, then says what she came to say.
    json j = Sample();
    j["items"] = json::array();
    j["steps"] = json::parse(R"([
      {"id": "a", "do": "stalk", "by": "p3", "why": "Uthgerd has trailed {player} since the gate, a warning on her lips", "goal": "Find out who is following me",
       "then": "b", "journal": "It was Uthgerd."},
      {"id": "b", "do": "talk", "with": "p3", "goal": "Hear Uthgerd out", "outcomes": [{"if": "Uthgerd tells what she knows", "then": "end:good", "journal": "She told me."}]}])");
    auto r = Tales::Compile(j, Town());
    if (!r.ok) std::cerr << r.errors;
    Require(r.ok, "a stalk compiles");
    const Arc& a = r.arc;
    Require(a.graph[0].kind == Kind::Stalk && a.cast[a.graph[0].who] == 0x103 && a.graph[0].marker == RNone && !a.graph[0].why.empty(), "who shadows, why, no marker");
    Require(a.graph[0].edges[0].when == If::Dead && a.graph[0].edges[1].when == If::Done, "their death ends it; caught, they talk");
    Book book;
    const auto id = book.Plan(a, 1.0f);
    book.Accept(id, 1.0f);
    Arc& t = *book.Find(id);
    Facts f;
    f.with[a.graph[0].who] = true;
    Require(!Advance(t, f, 1.1f).any(), "near is not yet met");
    f.spoke[a.graph[0].who] = true;
    Advance(t, f, 1.2f);
    Require(t.node == 1, "caught and spoken to");
    const auto bytes = Serialize(book);
    Book back, old;
    Require(Deserialize(bytes, back) && back.Find(id)->graph[0].kind == Kind::Stalk, "a stalk survives the co-save");
    Require(!Deserialize(bytes, old, 5), "a stalk step read as version 5 is refused, not misread");
    j["steps"][0]["by"] = "p0";
    Require(Tales::Compile(j, Town()).errors.find("does not shadow") != std::string::npos, "the asker came openly");
    j["steps"][0]["by"] = "p3";
    j["steps"][0].erase("why");
    Require(Tales::Compile(j, Town()).errors.find("needs \"why\"") != std::string::npos, "a reason to follow");
}

// Someone held at a place: who may be, where, the step that frees them, what is refused, and the co-save.
static json Taken() {
    return json::parse(R"({
      "title": "The Price of Brenuin", "genre": "adventure",
      "premise": "Hulda had Brenuin sold to the bandits of Embershard to be rid of what he owes her.",
      "people": [
        {"who": "p0", "part": "the one who asks", "truth": "Brenuin has been gone since last night. Nobody will say where."},
        {"who": "p1", "part": "the one taken", "truth": "Hulda's men walked me up to the mine and sold me for ten septims.", "held": "d2"},
        {"who": "p2", "part": "the one who sold him", "truth": "I had Brenuin taken to Embershard. He drank my cellar dry and laughed."}
      ],
      "offer": {"words": "Ysolda comes to {player}, white in the face.", "ask": "Find Brenuin for me.",
                "journal": "Ysolda asked me to find Brenuin.", "reward": 150, "days": 3},
      "steps": [
        {"id": "s1", "do": "talk", "with": "p2", "goal": "Ask Hulda when she last saw Brenuin",
         "outcomes": [{"if": "Hulda lets the mine slip", "then": "s2", "journal": "Hulda named Embershard."},
                      {"if": "Hulda says nothing", "then": "s2", "journal": "Hulda told me nothing."}]},
        {"id": "s2", "do": "slay", "place": "d2", "boss": "Grelka the Silent", "goal": "Kill Grelka the Silent", "then": "s3", "journal": "Grelka is dead."},
        {"id": "s3", "do": "free", "who": "p1", "goal": "Free Brenuin", "then": "s4", "journal": "I cut Brenuin loose."},
        {"id": "s4", "do": "talk", "with": "p1", "goal": "Hear from Brenuin how he came there",
         "outcomes": [{"if": "Brenuin names Hulda", "then": "s5", "journal": "Brenuin said Hulda sold him."},
                      {"if": "Brenuin is too afraid to say", "then": "end:other", "journal": "Brenuin would not say."}]},
        {"id": "s5", "do": "choose", "with": "p2", "goal": "Face Hulda with it", "question": "What now?",
         "options": [{"say": "The guard will hear of this.", "then": "end:good", "journal": "I told the guard."},
                     {"say": "Pay him what you owe, and it stays between us.", "then": "end:other", "journal": "Hulda paid."}]}
      ],
      "endings": {"good": {"journal": "Brenuin is home.", "rumor": "", "pay": true},
                  "bad": {"journal": "Brenuin is lost.", "rumor": "", "pay": false},
                  "other": {"journal": "It stays quiet.", "rumor": "", "pay": true}}
    })");
}

static Tales::Pool TownThatHolds() {
    auto pool = Town();
    pool.people[1].canHold = true;   // Brenuin: nobody the town cannot do without
    pool.depth = true;               // as for a story asked of the model
    return pool;
}

// A hunt for what is killing people: witnesses, what was left of a victim outside the place, then what did it.
static json Hunted() {
    return json::parse(R"({
      "title": "What Walks by Embershard", "genre": "mystery",
      "premise": "Hulda sent the hunter up to the mine with a message for the chief, and has kept quiet about it since he was found dead.",
      "people": [
        {"who": "p0", "part": "the one who asks", "truth": "A hunter I knew was found dead by the old mine. Nobody will say what did it."},
        {"who": "p2", "part": "the one who sent him", "truth": "I sent him up there with a message. I never thought they would kill him for it."}
      ],
      "offer": {"words": "Ysolda comes to {player}, shaken.", "ask": "Find out what killed him.",
                "journal": "Ysolda asked me to find out what killed the hunter.", "reward": 150, "days": 3},
      "steps": [
        {"id": "s1", "do": "talk", "with": "p2", "goal": "Ask Hulda what she knows of the hunter",
         "outcomes": [{"if": "Hulda says he went up to the mine", "then": "s2", "journal": "Hulda said he went up to the mine."},
                      {"if": "Hulda says nothing", "then": "s2", "journal": "Hulda told me nothing."}]},
        {"id": "s2", "do": "examine", "place": "d2", "what": "a hunter", "goal": "Look at what was left of the hunter",
         "clue": "He was cut down with plain steel, from behind. His purse and his boots are gone.", "then": "s3",
         "journal": "Cut down with steel and robbed: men did this, not beasts."},
        {"id": "s3", "do": "slay", "place": "d2", "boss": "Grelka the Silent", "goal": "Kill Grelka the Silent", "then": "s4", "journal": "Grelka is dead."},
        {"id": "s4", "do": "choose", "with": "p2", "goal": "Face Hulda with it", "question": "What now?",
         "options": [{"say": "The guard will hear of this.", "then": "end:good", "journal": "I told the guard."},
                     {"say": "It stays between us.", "then": "end:other", "journal": "I kept it to myself."}]}
      ],
      "endings": {"good": {"journal": "It is known what killed him.", "rumor": "", "pay": true},
                  "bad": {"journal": "Nobody knows what killed him.", "rumor": "", "pay": false},
                  "other": {"journal": "It stays quiet.", "rumor": "", "pay": true}}
    })");
}

static void Mystery() {
    const auto r = Tales::Compile(Hunted(), TownThatHolds());
    if (!r.ok) std::cerr << r.errors;
    Require(r.ok, "a hunt for what is killing people compiles");
    const Arc& a = r.arc;
    const Node& look = a.graph[1];
    Require(look.kind == Kind::Examine && look.site == 1 && look.prop == 0 && look.who == RNone && look.marker == RNone, "the step that looks at what was left");
    Require(a.clueWhat[0] == RemainsHunter && a.clueSite[0] == 1 && a.clueState[0] == ClueToPlace && a.clueRef[0] == 0 && a.clueWhat[1] == 0,
            "what lies there, and outside which place");
    Require(look.question.find("plain steel") != std::string::npos, "what is seen there is kept for the player");
    Require(look.why.empty(), "a dead one the story does not name carries no name");
    {
        json named = Hunted();
        named["steps"][1]["name"] = "Torvald";
        const auto rn = Tales::Compile(named, TownThatHolds());
        Require(rn.ok && rn.arc.graph[1].why == "Torvald", "the name the story gives the dead one goes with the step");
    }
    Book book;
    const auto id = book.Plan(a, 1.0f);
    book.Accept(id, 1.0f);
    Arc& t = *book.Find(id);
    Facts f;
    f.with[3] = f.spoke[3] = true;
    f.turn = 0;
    Advance(t, f, 1.1f);
    Require(t.node == 1, "the talk in town leads out to what was found");
    Require(!Advance(t, Facts{}, 1.2f).any(), "not done until it has been looked at");
    Facts seen;
    seen.examined[0] = true;
    const auto done = Advance(t, seen, 1.3f);
    Require(t.node == 2 && done.done.size() == 1 && done.line.find("men did this") != std::string::npos, "looked at: the clue is the player's, and the hunt is next");
    // Through the co-save.
    t.clueRef[0] = 0xFF001234;
    t.clueState[0] = CluePlaced;
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "what was left survives the co-save");
    const Arc& b = *back.Find(id);
    Require(b.clueWhat[0] == RemainsHunter && b.clueSite[0] == 1 && b.clueRef[0] == 0xFF001234 && b.clueState[0] == CluePlaced && b.graph[1].kind == Kind::Examine, "whole");
    Book seven;
    auto v7 = bytes;
    v7.resize(v7.size() - 14 - 12 - 21);
    Require(!Deserialize(v7, seven, 7), "a step that examines read as version 7 is refused, not misread");
    auto wrong = bytes;
    wrong[wrong.size() - 1 - 12 - 21] = 7;   // no such state
    Require(!Deserialize(wrong, seven), "a state that does not exist is refused");

    const auto errorsOf = [](json x, Tales::Pool pool) { return Tales::Compile(x, pool).errors; };
    json j = Hunted();
    j["steps"][1].erase("clue");
    Require(errorsOf(j, TownThatHolds()).find("what is seen on the remains") != std::string::npos, "a clue is something seen");
    j = Hunted();
    j["steps"][1]["what"] = "a dragon";
    Require(errorsOf(j, TownThatHolds()).find("a hunter, a cow, a horse, a goat or a deer") != std::string::npos, "only what the game can lay there");
    j = Hunted();
    j["steps"][2]["do"] = "delve";
    j["steps"][2].erase("boss");
    Require(errorsOf(j, TownThatHolds()).find("a later step is a \"slay\" there") != std::string::npos, "what did it is met at that place");
    j = Hunted();
    j["steps"][2]["place"] = "d1";
    Require(errorsOf(j, TownThatHolds()).find("a later step is a \"slay\" there") != std::string::npos, "at that place, not at another");
    j = Hunted();
    auto closed = TownThatHolds();
    for (auto& s : closed.sites) s.outside = false;
    Require(errorsOf(j, closed).find("nothing can be left outside") != std::string::npos, "only where there is ground outside it");
    auto barred = TownThatHolds();
    barred.barred = 1u << static_cast<unsigned>(Kind::Examine);
    Require(errorsOf(Hunted(), barred).find("no \"examine\" in this story") != std::string::npos, "not in every story");
    auto hunt = TownThatHolds();
    hunt.mustExamine = true;
    // A hunt has a trail (4.0): signs on the way, then the victim. One body by the door is not a hunt any more.
    Require(errorsOf(Hunted(), hunt).find("a hunt has a trail") != std::string::npos, "one thing to look at is no trail");
    const auto trailed = [](int signs) {
        json x = Hunted();
        static const char* kBeasts[] = {"a goat", "a horse", "a cow"};
        std::string next = "s2";
        for (int k = signs - 1; k >= 0; --k) {
            const std::string id = "t" + std::to_string(k);
            json sign = {{"id", id}, {"do", "examine"}, {"place", "d2"}, {"what", kBeasts[k % 3]}, {"goal", "Look at what was left of the beast"},
                         {"clue", "Its throat is cut clean, and nothing has fed on it."}, {"then", next}, {"journal", "A beast, killed and left."}};
            x["steps"].insert(x["steps"].begin() + 1, sign);
            next = id;
        }
        for (auto& o : x["steps"][0]["outcomes"]) o["then"] = next;
        return x;
    };
    {
        const auto two = Tales::Compile(trailed(1), hunt);
        Require(two.ok, "a hunt asked for, and it is one: a sign on the way, then the victim");
        Require(two.arc.clueWhat[0] == RemainsGoat && two.arc.clueWhat[1] == RemainsHunter && two.arc.clueFar[0] == 1 && two.arc.clueFar[1] == 0,
                "the first to be looked at lies farther out, the last by the place itself");
        const auto three = Tales::Compile(trailed(2), hunt);
        Require(three.ok && three.arc.clueFar[0] == 2 && three.arc.clueFar[1] == 1 && three.arc.clueFar[2] == 0 && three.arc.clueWhat[2] == RemainsHunter,
                "three along the way: farthest, nearer, by the place");
        Require(errorsOf(trailed(3), hunt).find("at most 3 things to examine") != std::string::npos, "three is the most");
        // The journal shows the way to the first sign only; the next is found by walking the trail.
        Book trail;
        const auto tid = trail.Plan(three.arc, 1.0f);
        Arc& w = *trail.Find(tid);
        w.clueState[1] = CluePlaced;
        w.clueRef[1] = 0xFF001235;
        w.sequelOf = 7;
        w.followed = 1;
        w.ends[0].after = "Grelka's brother is still up at the mine.";
        Book again;
        Require(Deserialize(Serialize(trail), again), "a trail survives the co-save");
        const Arc& kept = *again.Find(tid);
        Require(kept.clueFar[0] == 2 && kept.clueFar[1] == 1 && kept.clueFar[2] == 0 && kept.clueWhat[2] == RemainsHunter && kept.clueSite[2] == kept.clueSite[0] &&
                    kept.clueState[1] == CluePlaced && kept.clueRef[1] == 0xFF001235,
                "every sign with how far out it lies");
        Require(kept.sequelOf == 7 && kept.followed == 1 && kept.ends[0].after == "Grelka's brother is still up at the mine." && kept.ends[1].after.empty(),
                "and what an ending leaves open, and which tale took it up");
        auto broken = Serialize(trail);
        broken[broken.size() - 1] = 2;   // "followed" is yes or no
        Require(!Deserialize(broken, again), "a record that says neither is refused");
    }
    // What an ending leaves open, in the writer's words; nothing when it says nothing.
    {
        json x = Hunted();
        x["endings"]["other"]["after"] = "Hulda's part in it is known to {player} alone.";
        const auto res = Tales::Compile(x, TownThatHolds());
        Require(res.ok && res.arc.ends[2].after == "Hulda's part in it is known to {player} alone." && res.arc.ends[0].after.empty(), "what an ending leaves open");
    }
    // Lengths are counted in letters, and a text is cut at a sentence end of its own script.
    {
        json x = Hunted();
        std::string goal;
        for (int i = 0; i < 20; ++i) goal += "\xD0\x9F\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB2\xD0\xBE\xD1\x80\xD0\xB8 ";   // "Pogovori " in Cyrillic: 9 letters, 17 bytes, 20 times
        x["steps"][0]["goal"] = goal;
        auto res = Tales::Compile(x, TownThatHolds());
        Require(res.ok, "a long Cyrillic goal");
        std::size_t letters = 0;
        for (const unsigned char c : res.arc.graph[0].goal) letters += (c & 0xC0) != 0x80;
        Require(letters > 100 && letters <= 140, "140 letters are kept, not 140 bytes (which would be 74 letters)");
        std::string ja;
        for (int i = 0; i < 30; ++i) ja += "\xE7\x8B\xA9\xE4\xBA\xBA\xE3\x81\xAF\xE6\xAD\xBB\xE3\x82\x93\xE3\x81\xA0\xE3\x80\x82";   // six letters and the ideographic full stop
        x["steps"][0]["goal"] = ja;
        res = Tales::Compile(x, TownThatHolds());
        Require(res.ok, "a long Japanese goal");
        const std::string& cut = res.arc.graph[0].goal;
        Require(cut.size() >= 3 && cut.compare(cut.size() - 3, 3, "\xE3\x80\x82") == 0, "cut after an ideographic full stop, with nothing added");
        std::size_t jaLetters = 0;
        for (const unsigned char c : cut) jaLetters += (c & 0xC0) != 0x80;
        Require(jaLetters == 140, "at a sentence end within the limit: 20 sentences of seven letters");
    }
    Require(errorsOf(Taken(), hunt).find("a hunt for what is killing people") != std::string::npos, "and nothing else");
    // Naming what did it: a choice whose true answer the engine knows, worth a remedy from the one who hears it.
    {
        json n = Hunted();
        n["steps"][1]["then"] = "s2b";
        n["steps"].insert(n["steps"].begin() + 2, json::parse(R"({"id": "s2b", "do": "name", "place": "d2", "with": "p2",
            "goal": "Tell Hulda what killed him", "question": "So what killed him?",
            "truth": "Men with steel. The ones at the mine.", "others": ["Wolves, as the guard says.", "A bear come down from the hills."],
            "right": {"then": "s3", "journal": "I told Hulda it was the men at the mine."},
            "wrong": {"then": "s3", "journal": "I told Hulda what I thought. She did not look as if she believed it."}})"));
        const auto rn = Tales::Compile(n, TownThatHolds());
        if (!rn.ok) std::cerr << rn.errors;
        Require(rn.ok, "a hunt in which the player names what did it compiles");
        const Node& name = rn.arc.graph[2];
        Require(name.kind == Kind::Choose && name.who == 2 && name.site == 1 && name.question == "So what killed him?", "to the engine it is a choice, by that place");
        int truthAt = -1, remedies = 0, plain = 0;
        for (int k = 0; k < 3; ++k) {
            if (name.options[static_cast<std::size_t>(k)].find("Men with steel") != std::string::npos) truthAt = k;
        }
        Require(truthAt >= 0, "the true answer is among the three");
        for (const auto& e : name.edges) {
            if (e.when != If::Option0 && e.when != If::Option1 && e.when != If::Option2) continue;
            const int k = static_cast<int>(e.when) - static_cast<int>(If::Option0);
            if (k == truthAt) {
                Require(e.effect == Effect::Remedy && e.effectRole == 2 && e.prop == 0 && e.next == 3 && e.line.find("men at the mine") != std::string::npos,
                        "the truth: the one who hears it gives what they have against it");
                ++remedies;
            } else {
                Require(e.effect == Effect::None && e.next == 3 && e.line.find("did not look as if") != std::string::npos, "a wrong answer costs the help, nothing else");
                ++plain;
            }
        }
        Require(remedies == 1 && plain == 2, "one truth, two that are not");
        Require(Tales::Compile(n, TownThatHolds()).arc.graph[2].options == name.options, "the same story lays its answers out the same way");
        Book nb;
        const auto nid = nb.Plan(rn.arc, 1.0f);
        nb.Accept(nid, 1.0f);
        Arc& na = *nb.Find(nid);
        na.node = 2;
        Facts say;
        say.with[2] = say.spoke[2] = true;
        say.answer = truthAt;
        const auto told = Advance(na, say, 1.1f);
        Require(na.node == 3 && told.effect == Effect::Remedy && told.effectProp == 0 && told.effectRole == 2, "named rightly: the remedy for that place is given");
        const auto bytes2 = Serialize(nb);
        Book nback;
        Require(Deserialize(bytes2, nback), "a remedy on a branch survives the co-save");

        const auto errorsOf2 = [](json x) { return Tales::Compile(x, TownThatHolds()).errors; };
        json e1 = n;
        e1["steps"][2].erase("truth");
        Require(errorsOf2(e1).find("the answer that is true") != std::string::npos, "a truth is needed");
        json e2 = n;
        e2["steps"][2]["others"] = json::array({"Wolves."});
        Require(errorsOf2(e2).find("two answers that are false") != std::string::npos, "and two that are false");
        json e3 = n;
        e3["steps"][0]["outcomes"][0]["then"] = "s2b";
        e3["steps"][0]["outcomes"][1]["then"] = "s2b";
        e3["steps"][2]["right"]["then"] = "s2";
        e3["steps"][2]["wrong"]["then"] = "s2";
        e3["steps"][1]["then"] = "s3";
        Require(errorsOf2(e3).find("once its victim has been looked at") != std::string::npos, "named only after looking");
        json e4 = n;
        e4["steps"][3]["do"] = "delve";
        e4["steps"][3].erase("boss");
        Require(errorsOf2(e4).find("\"slay\" there") != std::string::npos, "and faced after it is named");
    }
    // The depth every story is held to counts it as an act out in the world.
    auto deep = TownThatHolds();
    deep.depth = true;
    const auto rd = Tales::Compile(Hunted(), deep);
    if (!rd.ok) std::cerr << rd.errors;
    Require(rd.ok, "a hunt among people, with a choice after it, is deep enough");
}

// A haunting: people who will not speak of the dead one, the thing she wants, and her shade by night.
static json Haunted() {
    return json::parse(R"({
      "title": "The Pale Widow", "genre": "the uncanny",
      "premise": "Hulda took the dead woman's ring from her hand before the body was carried out, and has let the town believe the ring was lost.",
      "people": [
        {"who": "p0", "part": "the one who asks", "truth": "My aunt Hilde died out by the mine last winter. Now people say she walks there after dark."},
        {"who": "p2", "part": "the one who kept it", "truth": "I took Hilde's ring from her hand before they carried her out. I have it at home."}
      ],
      "items": [{"id": "i1", "item": "Silver Ring", "name": "Hilde's Ring", "hidden": "home", "of": "p2"}],
      "offer": {"words": "Ysolda comes to {player}, pale.", "ask": "Find out why my aunt does not rest.",
                "journal": "Ysolda asked me to find out why her aunt's shade walks by the mine.", "reward": 150, "days": 4},
      "steps": [
        {"id": "s1", "do": "talk", "with": "p2", "goal": "Ask Hulda about the night Hilde died",
         "outcomes": [{"if": "Hulda admits she has the ring", "then": "s2", "journal": "Hulda has the ring."},
                      {"if": "Hulda says nothing", "then": "s3", "journal": "Hulda told me nothing."}]},
        {"id": "s2", "do": "find", "item": "i1", "goal": "Find Hilde's ring in Hulda's home", "then": "s3", "journal": "I have Hilde's ring."},
        {"id": "s3", "do": "haunt", "place": "d2", "spirit": "the Pale Widow", "sex": "woman", "wants": "i1",
         "goal": "Go to the mine after dark and face what walks there",
         "truth": "I was Hilde. I froze out here waiting for a man who never came, and someone took my ring from my hand before I was cold. I want it back.",
         "rested": {"then": "s4", "journal": "I gave her the ring, and she was gone."},
         "banished": {"then": "s4", "journal": "I struck her down. The mine is quiet."}},
        {"id": "s4", "do": "choose", "with": "p2", "goal": "Face Hulda with it", "question": "What now?",
         "options": [{"say": "The town will hear whose ring you kept.", "then": "end:good", "journal": "I told the town."},
                     {"say": "It stays between us.", "then": "end:other", "journal": "I kept it to myself."}]}
      ],
      "endings": {"good": {"journal": "She rests, and it is known why she did not.", "rumor": "", "pay": true},
                  "bad": {"journal": "Nobody knows why she walked.", "rumor": "", "pay": false},
                  "other": {"journal": "It stays quiet.", "rumor": "", "pay": true}}
    })");
}

static void Haunting() {
    const auto r = Tales::Compile(Haunted(), TownThatHolds());
    if (!r.ok) std::cerr << r.errors;
    Require(r.ok, "a haunting compiles");
    const Arc& a = r.arc;
    const Node& walk = a.graph[2];
    const auto has = [](const Node& n, If when) {
        return std::any_of(n.edges.begin(), n.edges.end(), [when](const Edge& e) { return e.when == when; });
    };
    Require(walk.kind == Kind::Haunt && walk.site == 1 && walk.who == RNone && has(walk, If::Rested) && has(walk, If::Done), "the step that faces what walks: two ways on");
    Require(a.spiritSite == 1 && a.spiritWoman == 1 && a.spiritWants == 1 && a.spiritState == SpiritAway && a.spiritRef == 0 &&
            a.spiritName == "the Pale Widow" && a.spiritTruth.find("I was Hilde") != std::string::npos, "what walks, where, and what it wants");
    Require(IsNight(22.0f) && IsNight(2.0f) && !IsNight(12.0f) && !IsNight(5.0f) && IsNight(21.0f), "night is from nine to five");

    // Struck down.
    {
        Book book;
        const auto id = book.Plan(a, 1.0f);
        book.Accept(id, 1.0f);
        Arc& t = *book.Find(id);
        Facts f;
        f.with[3] = f.spoke[3] = true;
        f.turn = 1;
        Advance(t, f, 1.1f);
        Require(t.node == 2, "told nothing, the way leads to the shade all the same");
        Facts night;
        night.spirit = SpiritOut;
        Require(!Advance(t, night, 1.2f).any(), "out there and unharmed: nothing is done yet");
        night.spirit = SpiritBanished;
        const auto done = Advance(t, night, 1.3f);
        Require(t.node == 3 && done.line.find("struck her down") != std::string::npos, "struck down: the story goes on by that way");
    }
    // Laid to rest, and through the co-save while it walks.
    {
        Book book;
        const auto id = book.Plan(a, 1.0f);
        book.Accept(id, 1.0f);
        Arc& t = *book.Find(id);
        Facts f;
        f.with[3] = f.spoke[3] = true;
        f.turn = 0;
        Advance(t, f, 1.1f);
        Facts got;
        got.has[0] = true;
        Advance(t, got, 1.2f);
        Require(t.node == 2, "with the ring in hand, to the shade");
        t.spiritRef = 0xFF00ABCD;
        t.spiritState = SpiritOut;
        const auto bytes = Serialize(book);
        Book back;
        Require(Deserialize(bytes, back), "what walks survives the co-save");
        const Arc& b = *back.Find(id);
        Require(b.spiritSite == 1 && b.spiritWoman == 1 && b.spiritWants == 1 && b.spiritState == SpiritOut && b.spiritRef == 0xFF00ABCD &&
                b.spiritName == "the Pale Widow" && b.spiritTruth == t.spiritTruth && b.graph[2].kind == Kind::Haunt, "whole");
        Book eight;
        Require(!Deserialize(bytes, eight, 8), "a haunting read as version 8 is refused, not misread");
        Facts rest;
        rest.spirit = SpiritRested;
        const auto done = Advance(t, rest, 1.3f);
        Require(t.node == 3 && done.line.find("gave her the ring") != std::string::npos, "laid to rest: the story goes on by the other way");
    }

    const auto errorsOf = [](json x, Tales::Pool pool) { return Tales::Compile(x, pool).errors; };
    json j = Haunted();
    j["steps"][2].erase("banished");
    Require(errorsOf(j, TownThatHolds()).find("strikes it down") != std::string::npos, "it can always be ended by force: the story is never stuck");
    j = Haunted();
    j["steps"][2].erase("truth");
    Require(errorsOf(j, TownThatHolds()).find("in its own words") != std::string::npos, "the dead one has words of their own");
    j = Haunted();
    j["steps"][2]["sex"] = "wolf";
    Require(errorsOf(j, TownThatHolds()).find("\"woman\" or \"man\"") != std::string::npos, "the shade of a person");
    j = Haunted();
    j["steps"][2]["wants"] = "i9";
    Require(errorsOf(j, TownThatHolds()).find("names no item of the tale") != std::string::npos, "it wants a thing of the story");
    j = Haunted();
    j["steps"][2].erase("wants");
    Require(errorsOf(j, TownThatHolds()).find("\"rested\" needs \"wants\"") != std::string::npos, "nothing rests for nothing");
    j = Haunted();
    j["steps"][1]["do"] = "wait";
    j["steps"][1].erase("item");
    j["steps"][1]["hours"] = 2;
    Require(errorsOf(j, TownThatHolds()).find("must be in {player}'s hands by then") != std::string::npos, "what lets it rest can be had before");
    {
        json plain = Haunted();
        plain["steps"][2].erase("wants");
        plain["steps"][2].erase("rested");
        const auto rp = Tales::Compile(plain, TownThatHolds());
        Require(rp.ok && rp.arc.spiritWants == 0 && !has(rp.arc.graph[2], If::Rested), "a shade that wants nothing can only be struck down");
    }
    auto closed = TownThatHolds();
    for (auto& s : closed.sites) s.outside = false;
    Require(errorsOf(Haunted(), closed).find("nothing walks outside") != std::string::npos, "only where there is ground outside it");
    auto barred = TownThatHolds();
    barred.barred = 1u << static_cast<unsigned>(Kind::Haunt);
    Require(errorsOf(Haunted(), barred).find("no \"haunt\" in this story") != std::string::npos, "not in every story");
    auto asked = TownThatHolds();
    asked.mustHaunt = true;
    Require(Tales::Compile(Haunted(), asked).ok, "a haunting asked for, and it is one");
    Require(errorsOf(Hunted(), asked).find("this story is a haunting") != std::string::npos, "and nothing else");
    j = Haunted();
    j["steps"].push_back(j["steps"][2]);
    j["steps"][4]["id"] = "s5";
    Require(errorsOf(j, TownThatHolds()).find("one thing that walks by night") != std::string::npos, "one in a story");
}

static void Captive() {
    const auto r = Tales::Compile(Taken(), TownThatHolds());
    if (!r.ok) std::cerr << r.errors;
    Require(r.ok, "a story with someone held compiles");
    const Arc& a = r.arc;
    Require(a.captive == 2 && a.cast[a.captive] == 0x101 && a.captiveSite == 1 && a.site[0] == 0x710 && a.captiveState == CaptiveToTake,
            "who is held, and where");
    Require(a.graph[2].kind == Kind::Free && a.graph[2].who == 2 && a.graph[2].site == 1 && a.graph[2].marker == 2, "the step that frees them");
    Book book;
    const auto id = book.Plan(a, 1.0f);
    book.Accept(id, 1.0f);
    Arc& t = *book.Find(id);
    Facts f;
    f.with[3] = f.spoke[3] = true;
    f.turn = 0;
    Advance(t, f, 1.1f);
    Require(t.node == 1, "the talk in town leads out to the place");
    Facts dead;
    dead.bossDead[0] = true;
    Advance(t, dead, 1.2f);
    Require(t.node == 2, "the master dead: now to the one held");
    Require(!Advance(t, dead, 1.3f).any(), "not free until the player stands by them");
    Facts reached;
    reached.captiveReached = true;
    Advance(t, reached, 1.4f);
    Require(t.node == 3, "reached: free, and the talk with them is next");
    // Found before the story asked: the steps that led there are left, and the step that frees is done at once.
    {
        const auto has = [](const Node& n, If when) {
            return std::any_of(n.edges.begin(), n.edges.end(), [when](const Edge& e) { return e.when == when; });
        };
        Require(a.graph[0].edges[0].when == If::Freed && a.graph[0].edges[0].skip && a.graph[0].edges[0].next == 2 &&
                a.graph[0].edges[1].when == If::Dead, "a step with someone: found already comes before their death");
        Require(has(a.graph[1], If::Freed) && a.graph[1].edges[0].when == If::Done, "a way down that was gone all the same is done, not left");
        Require(!has(a.graph[2], If::Freed) && !has(a.graph[3], If::Freed), "nothing to leave at the step that frees, or after it");
        Book early;
        const auto eid = early.Plan(a, 1.0f);
        early.Accept(eid, 1.0f);
        Arc& e = *early.Find(eid);
        Facts found;
        found.captiveReached = true;
        found.dead[3] = true;   // and the witness nobody needs any more has died
        const auto left = Advance(e, found, 1.1f);
        Require(e.node == 2 && left.skipped == 0 && left.done.empty() && left.ending == Ending::None && left.line.empty() && e.heard[3].empty(),
                "the talk in town is left: not done, no line, nothing for the witness to remember");
        const auto freed = Advance(e, found, 1.2f);
        Require(e.node == 3 && freed.done.size() == 1 && freed.done.front() == 2, "and the step that frees is done");
        Book mid;
        const auto mid_id = mid.Plan(a, 1.0f);
        mid.Accept(mid_id, 1.0f);
        Arc& m = *mid.Find(mid_id);
        Facts talk;
        talk.with[3] = talk.spoke[3] = true;
        talk.turn = 0;
        Advance(m, talk, 1.1f);
        Facts both;
        both.captiveReached = true;
        both.bossDead[0] = true;
        const auto down = Advance(m, both, 1.2f);
        Require(m.node == 2 && down.done.size() == 1 && down.skipped == kNoNode, "the master dead and they free: the step is done");
        // Two ways of freeing them: which one happened cannot be said, so no shortcut.
        json two = Taken();
        json other = two["steps"][2];
        other["id"] = "s9";
        two["steps"].push_back(other);
        two["steps"][0]["outcomes"][1]["then"] = "s9";
        const auto r2 = Tales::Compile(two, TownThatHolds());
        if (!r2.ok) std::cerr << r2.errors;
        Require(r2.ok, "a story with two ways of freeing them compiles");
        bool any = false;
        for (const auto& n : r2.arc.graph) any = any || has(n, If::Freed);
        Require(!any, "two steps that free: no shortcut to either");
    }
    // Through the co-save, with where they stand.
    t.captiveState = CaptiveBound;
    const auto bytes = Serialize(book);
    Book back;
    Require(Deserialize(bytes, back), "someone held survives the co-save");
    Require(back.Find(id)->captive == 2 && back.Find(id)->captiveSite == 1 && back.Find(id)->captiveState == CaptiveBound &&
            back.Find(id)->graph[2].kind == Kind::Free, "whole");
    Book six;
    auto v6 = bytes;
    v6.resize(v6.size() - 3 - 14 - 12 - 21);   // without the three bytes of someone held, the fourteen of the remains and the twelve of what walks by night after them
    Require(!Deserialize(v6, six, 6), "a step that frees someone read as version 6 is refused, not misread");
    auto wrong = bytes;
    wrong[wrong.size() - 1 - 14 - 12 - 21] = 9;   // no such state
    Require(!Deserialize(wrong, six), "a state that does not exist is refused");

    const auto errorsOf = [](json x, Tales::Pool pool) { return Tales::Compile(x, pool).errors; };
    json j = Taken();
    Require(errorsOf(j, Town()).find("cannot be held anywhere") != std::string::npos, "only someone the town can do without");
    j["people"][1]["held"] = "d1";
    Require(errorsOf(j, TownThatHolds()).find("keeps no prisoners") != std::string::npos, "a Dwemer ruin holds nobody");
    j = Taken();
    j["people"][0]["held"] = "d2";
    Require(errorsOf(j, TownThatHolds()).find("is not the one held") != std::string::npos, "never the one who asks");
    j = Taken();
    j["steps"][0]["with"] = "p1";   // a talk with the one held, before anyone has freed them
    Require(errorsOf(j, TownThatHolds()).find("is held until a step frees them") != std::string::npos, "nothing is done with them before");
    j = Taken();
    j["steps"][2]["do"] = "delve";
    j["steps"][2]["place"] = "d2";
    Require(errorsOf(j, TownThatHolds()).find("no step frees them") != std::string::npos, "held, and never freed");
    j = Taken();
    j["people"][1].erase("held");
    Require(errorsOf(j, TownThatHolds()).find("nobody of the story is held") != std::string::npos, "a step that frees nobody");
    j = Taken();
    j["steps"][2]["who"] = "p2";
    Require(errorsOf(j, TownThatHolds()).find("the one who is held") != std::string::npos, "the one to free is the one held");
    // Told lately: none this time.
    auto barred = TownThatHolds();
    barred.barred = 1u << static_cast<unsigned>(Kind::Free);
    Require(errorsOf(Taken(), barred).find("no \"free\" in this story") != std::string::npos, "not in every story");
    // A rescue was asked for: a story without someone held is not it.
    auto rescue = TownThatHolds();
    rescue.mustCaptive = true;
    Require(errorsOf(Taken(), rescue).find("must be close to the one who asks") != std::string::npos, "a rescue is for someone who is theirs");
    rescue.people[1].close = true;   // Brenuin lodges under Ysolda's roof in this town
    Require(Tales::Compile(Taken(), rescue).ok, "a rescue is a story with someone held who is close to the asker");
    Require(errorsOf(Sample(), rescue).find("someone held at one of the places") != std::string::npos, "and nothing else");
}

// What a story asked of the model is held to: ways that end differently, and a place as one act among people.
static void Depth() {
    auto pool = Town();
    pool.depth = true;
    const auto errorsOf = [&](const json& x) { return Tales::Compile(x, pool).errors; };
    Require(Tales::Compile(Sample(), pool).ok, "the sample has two endings and stays among people");
    // The errand: into the mine, kill, take, hand over.
    json j = Sample();
    j["items"] = json::parse(R"([{"id": "i1", "item": "Silver Ring", "name": "Ysolda's Ring", "hidden": "at", "of": "d2"}])");
    j["steps"] = json::parse(R"([
      {"id": "a", "do": "delve", "place": "d2", "goal": "Go into Embershard Mine", "then": "b", "journal": "In."},
      {"id": "b", "do": "slay", "place": "d2", "boss": "Grelka the Silent", "goal": "Kill Grelka", "then": "c", "journal": "Dead."},
      {"id": "c", "do": "find", "item": "i1", "goal": "Take the ring from Grelka", "then": "e", "journal": "Mine."},
      {"id": "e", "do": "give", "item": "i1", "to": "p0", "goal": "Give Ysolda her ring", "then": "end:good", "journal": "Hers."}])");
    const auto errand = errorsOf(j);
    Require(errand.find("leads to the same ending") != std::string::npos, "one ending is no story");
    Require(errand.find("never the whole of it") != std::string::npos, "a place alone is no story");
    Require(errand.find("the story ends at the place") != std::string::npos, "fetching and handing over is no story");
    Require(Tales::Compile(j, Town()).ok, "a test's own tale is not held to it");
    // The same place as one act: a talk before, and what is found forces a choice after.
    j["steps"] = json::parse(R"([
      {"id": "t", "do": "talk", "with": "p1", "goal": "Ask Brenuin who he sold the ring to",
       "outcomes": [{"if": "Brenuin names Grelka", "then": "b", "journal": "Grelka has it."}, {"if": "Brenuin lies", "then": "end:bad", "journal": "He lied."}]},
      {"id": "b", "do": "slay", "place": "d2", "boss": "Grelka the Silent", "goal": "Kill Grelka", "then": "c", "journal": "Dead."},
      {"id": "c", "do": "find", "item": "i1", "goal": "Take the ring from Grelka", "then": "d", "journal": "Mine."},
      {"id": "d", "do": "choose", "with": "p0", "goal": "Tell Ysolda who sold it", "question": "What do you tell her?",
       "options": [{"say": "Brenuin sold it.", "then": "end:good", "journal": "She knows."}, {"say": "I found it in the mine.", "then": "end:other", "journal": "I kept it from her."}]}])");
    j["endings"]["other"] = json::parse(R"({"journal": "Ysolda never learned.", "rumor": "", "pay": true})");
    const auto act = Tales::Compile(j, pool);
    if (!act.ok) std::cerr << act.errors;
    Require(act.ok, "a place as one act among people");
    // The engine's count says this one goes out into the world.
    pool.mustPlace = true;
    Require(errorsOf(Sample()).find("leads out into the world") != std::string::npos, "a story in town when one out there is due");
    Require(Tales::Compile(j, pool).ok, "one with a place is it");
}

static void LongWords() {
    // An answer longer than the popup holds keeps what reads whole: up to its last clause...
    json j = Sample();
    j["steps"][2]["options"][1]["say"] = "Tell her plainly that Saadia is innocent and that she herself was swindled by Quickhand, and that the casks were watered from the first day she bought them";
    auto r = Tales::Compile(j, Town());
    Require(r.ok, "a long answer still compiles");
    std::string said;
    for (const auto& n : r.arc.graph) if (n.kind == Kind::Choose) said = n.options[1];
    Require(said == "Tell her plainly that Saadia is innocent and that she herself was swindled by Quickhand.", "cut at the last clause");
    // ...or its last full sentence...
    j["steps"][2]["options"][1]["say"] = "Tell her plainly that Saadia is innocent and that she herself was swindled. Then say the casks were watered from the first day she bought them and never once since";
    r = Tales::Compile(j, Town());
    for (const auto& n : r.arc.graph) if (n.kind == Kind::Choose) said = n.options[1];
    Require(said == "Tell her plainly that Saadia is innocent and that she herself was swindled.", "cut at the last sentence");
    // ...and with no such place, it is cut at the end of a word and says so.
    j["steps"][2]["options"][1]["say"] = "Tell her plainly that Saadia is innocent and that she herself was swindled by Quickhand and that the casks were watered from the first day she bought them";
    r = Tales::Compile(j, Town());
    for (const auto& n : r.arc.graph) if (n.kind == Kind::Choose) said = n.options[1];
    const std::string whole = j["steps"][2]["options"][1]["say"].get<std::string>();
    const std::string kept = said.size() >= 3 ? said.substr(0, said.size() - 3) : std::string();
    Require(said.size() <= 140 && said.ends_with("...") && !kept.empty() && whole.starts_with(kept) && whole[kept.size()] == ' ' &&
                kept.back() != ' ' && kept.back() != ',',
            "cut at a word, marked as cut");
    // The same for what an asker says in the offer box.
    j = Sample();
    j["offer"]["ask"] = "Please, you look like someone with a steady hand and no stake in this town's quarrels; speak with Embry for me and find out what he really knows about the song I sang, because I cannot go near him without paying and I cannot pay.";
    r = Tales::Compile(j, Town());
    Require(r.ok && r.arc.offerAsk.ends_with("what he really knows about the song I sang."), "a plea ends where a clause ends");
}

static void Refusals() {
    const auto errorsOf = [](json j) { return Tales::Compile(j, Town()).errors; };
    json j = Sample();
    j["people"].push_back(json::parse(R"({"who": "p2", "part": "the innkeeper", "truth": "I keep the inn."})"));
    j["steps"][4]["with"] = "p2";   // Hulda is no brawler
    Require(errorsOf(j).find("no one to brawl with") != std::string::npos, "an innkeeper is never brawled with");
    j = Sample();
    j["steps"][2]["options"][1]["effect"] = "jail p0";
    Require(errorsOf(j).find("cannot be taken by the guard") != std::string::npos, "the asker is never jailed");
    j = Sample();
    j["people"][2]["who"] = "p9";
    Require(!errorsOf(j).empty(), "a stranger is refused");
    j = Sample();
    j["items"][0]["of"] = "p3";
    Require(errorsOf(j).find("no home") != std::string::npos, "nothing hidden in a home someone does not have");
    j = Sample();
    j["steps"][0]["outcomes"][0]["then"] = "s7";
    Require(errorsOf(j).find("no step") != std::string::npos, "a branch to nowhere is refused");
    j = Sample();
    j["steps"] = json::parse(R"([{"id": "a", "do": "talk", "with": "p1", "goal": "Talk", "outcomes": [{"if": "x", "then": "stay"}]}])");
    Require(errorsOf(j).find("no way from it leads to an ending") != std::string::npos, "a story that cannot end is refused");
    j = Sample();
    j["endings"].erase("bad");
    Require(errorsOf(j).find("\"bad\"") != std::string::npos, "both endings are written");
    j = Sample();
    j["steps"][0]["outcomes"][1]["then"] = "end:other";
    Require(errorsOf(j).find("end:other") != std::string::npos, "an ending used must be written");
    Require(!Tales::Compile(json::array(), Town()).ok, "not an object");
    j = Sample();
    j["offer"]["words"] = "The player looks lost.";
    const auto ok = Tales::Compile(j, Town());
    Require(ok.ok && ok.arc.offerWords == "{player} looks lost.", "\"the player\" becomes the player's name");
    // People named by name, also in effects of several words.
    j = Sample();
    j["people"][0]["who"] = "Brenuin";
    j["steps"][2]["options"][0]["effect"] = "befriend Brenuin and Uthgerd";
    j["steps"][2]["options"][1]["effect"] = "player pays Uthgerd 50";
    const auto named = Tales::Compile(j, Town());
    Require(named.ok && named.arc.cast[2] == 0x101, "a person named by name");
    Require(named.arc.graph[2].edges[1].effectRole == 2 && named.arc.graph[2].edges[1].bondWith == 3, "befriend two named people");
    Require(named.arc.price == 50 && named.arc.graph[2].edges[3].effectRole == 3, "the player pays a named person");
}

static void Titles() {
    // Three stories running came back as "The Quiet Order", "The Quiet Husband", "The Quiet Ledger" (2026-11-22): a
    // new title shares no word of five letters or more with the titles told lately.
    Tales::Pool pool = Town();
    pool.titles = {"The Quiet Husband", "A Debt of Salt"};
    json j = Sample();
    j["title"] = "The Quiet Ledger";
    auto r = Tales::Compile(j, pool);
    Require(!r.ok && r.errors.find("\"Quiet\" stood in the title of an earlier story already (\"The Quiet Husband\")") != std::string::npos,
            "a word of an earlier title is refused, and the writer is told which");
    // In any case, in any script.
    j["title"] = "QUIET waters";
    Require(!Tales::Compile(j, pool).ok, "whatever the case");
    pool.titles = {"Тихий муж"};
    j["title"] = "ТИХИЙ долг";
    r = Tales::Compile(j, pool);
    Require(!r.ok && r.errors.find("ТИХИЙ") != std::string::npos, "Cyrillic too, named as written");
    // Short words are everybody's ("The", "of", "Debt").
    pool.titles = {"The Quiet Husband", "A Debt of Salt"};
    j["title"] = "The Debt of Honour";
    Require(Tales::Compile(j, pool).ok, "short words are free");
    // So are the names of the story's own people and places: a story that follows another may be named after the
    // same person, in whatever form the language gives the name.
    pool.titles = {"Brenuin's Bottle", "The Road to Mzinchaleft"};
    j["title"] = "Brenuin's Promise";
    Require(Tales::Compile(j, pool).ok, "a name of the cast is free");
    j["title"] = "Under Mzinchaleft";
    Require(Tales::Compile(j, pool).ok, "a name of a place is free");
    j["title"] = "The Bottle of Brenuin";
    Require(!Tales::Compile(j, pool).ok, "but not the other words beside it");
    // A script that writes no spaces between words is not held to it (its words cannot be told apart here).
    pool.titles = {"静かな夫の秘密"};
    j["title"] = "静かな夫の秘密";
    Require(Tales::Compile(j, pool).ok, "unspaced scripts are not held to it");
    // The second asking keeps the tale whatever its title.
    pool.titles = {"The Quiet Husband"};
    pool.titleMayRepeat = true;
    j["title"] = "The Quiet Ledger";
    Require(Tales::Compile(j, pool).ok, "a title is no reason to lose a tale asked for twice");
    // Nothing told yet: anything goes.
    Require(Tales::Compile(j, Town()).ok, "no titles, no rule");
}

int main() {
    try {
        CompilesAndRuns();
        BrawlAndWaitAndDeath();
        PaidInPerson();
        CloserAndApart();
        HandsOver();
        Adventure();
        Ambush();
        Stalk();
        Captive();
        Mystery();
        Haunting();
        Depth();
        LongWords();
        Refusals();
        Titles();
    } catch (const std::exception& e) {
        std::cerr << "TalesCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "TalesCheck passed\n";
    return 0;
}
