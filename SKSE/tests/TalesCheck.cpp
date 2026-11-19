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
    v4.resize(v4.size() - (3 * 4 + 2 + 1));
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
    Require(Deserialize(bytes, five, 5) && five.Find(id)->graph[0].kind == Kind::Ambush, "a record of version 5 loads");

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
        LongWords();
        Refusals();
    } catch (const std::exception& e) {
        std::cerr << "TalesCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "TalesCheck passed\n";
    return 0;
}
