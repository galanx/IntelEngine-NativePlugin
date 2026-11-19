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
    Require(v.ending == Ending::Done, "he came and talked: done");
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

    j["steps"][0]["place"] = "Blackreach";
    Require(Tales::Compile(j, Town()).errors.find("no place of the list") != std::string::npos, "only the places offered");
    j["steps"][0]["place"] = "d3";
    j["steps"][1]["place"] = "d1";   // a third place: Embershard (the ring), Bleakwind Basin, Mzinchaleft
    Require(Tales::Compile(j, Town()).errors.find("at most 2 places") != std::string::npos, "two places at most");
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
        HandsOver();
        Adventure();
        Refusals();
    } catch (const std::exception& e) {
        std::cerr << "TalesCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "TalesCheck passed\n";
    return 0;
}
