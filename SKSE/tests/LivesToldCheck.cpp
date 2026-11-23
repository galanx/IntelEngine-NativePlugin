// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/LivesToldCheck.cpp
//
// What passed while apart, told (LivingWorld.h): the reunion's rules, how an adventure of their own ends, the words
// of the reunion, the keepsake and the talk of the day, the talk carried by the rumor ledger to other holds and kept
// from the ears of the one it is about, and the co-save record that keeps it all (version 3, version 2 still read).
#include "LivingWorld.h"
#include "Rumors.h"

#include <cctype>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace LW = IntelEngine::LivingWorld;
namespace RU = IntelEngine::Rumors;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// Nothing of the template left, and never the word "player" (characters do not know who that is).
static void Clean(const std::string& text, const char* what) {
    std::string lower = text;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (Has(text, "{") || Has(text, "}") || Has(lower, "player")) {
        throw std::runtime_error(std::string(what) + ": " + text);
    }
}

// Places, people and items by number, as the game would name them.
static LW::Names Fake() {
    static const std::map<std::uint32_t, std::string> places = {
        {10, "the Bannered Mare in Whiterun"}, {11, "the Sleeping Giant Inn in Riverwood"}, {20, "Fellglow Keep"}, {30, "Riften"}, {40, "Whiterun"}};
    static const std::map<std::uint32_t, std::string> people = {{1, "Uthgerd"}, {2, "Lydia"}};
    static const std::map<std::uint32_t, std::string> items = {{0x63B42, "Ruby"}, {0x63B44, "Amethyst"}};
    LW::Names n;
    n.place = [](std::uint32_t id) { const auto it = places.find(id); return it == places.end() ? std::string() : it->second; };
    n.person = [](std::uint32_t id) { const auto it = people.find(id); return it == people.end() ? std::string() : it->second; };
    n.item = [](std::uint32_t id) { const auto it = items.find(id); return it == items.end() ? std::string() : it->second; };
    n.player = "Albrecht";
    n.self = "Uthgerd";
    return n;
}

static LW::Life Uthgerd() {
    LW::Life life;
    life.actor = 1;
    life.flags = LW::kWasFollower;
    life.parkedSince = 100.0f;
    life.parkedAt = 40;
    life.lastMet = 100.0f;
    return life;
}

static LW::PastStep Step(LW::Activity activity, std::uint32_t place, float at, std::uint32_t companion = 0) {
    LW::PastStep s;
    s.activity = activity;
    s.place = place;
    s.at = at;
    s.companion = companion;
    return s;
}

static void Reunions() {
    LW::Life life = Uthgerd();
    Require(!LW::BeginsReunion(life, 102.9f, false), "under three days apart is no reunion");
    Require(LW::BeginsReunion(life, 103.0f, false), "three days apart, meeting again is a reunion");
    Require(!LW::BeginsReunion(life, 110.0f, true), "someone at the player's side has no reunion to make");
    LW::Life stranger = life;
    stranger.flags = 0;
    Require(!LW::BeginsReunion(stranger, 110.0f, false), "only former followers");
    LW::Life never = life;
    never.parkedSince = -1.0f;
    Require(!LW::BeginsReunion(never, 110.0f, false), "never left behind: nothing passed");
    LW::Life errand = life;
    errand.flags |= LW::kOnTask;
    Require(!LW::BeginsReunion(errand, 110.0f, false), "away on the player's errand: the errand is their meeting");

    // Begun when the player came near, at day 112 after 12 days apart.
    life.metAgainAt = 112.0f;
    life.apartDays = 12.0f;
    Require(LW::ReunionOpen(life, 112.0f) && LW::ReunionOpen(life, 113.9f), "not greeted yet: it waits a while for the two to meet");
    Require(!LW::ReunionOpen(life, 114.1f) && !LW::ReunionOpen(life, 111.0f), "and not for ever, nor before it began");
    LW::ReunionTold(life, 112.5f);
    Require(LW::Has(life, LW::kReunionTold) && life.metAgainAt == 112.5f && life.apartDays == 12.5f, "greeted: apart counted to the greeting");
    Require(LW::words::LastSeen(life) == 100.0f, "when they last saw each other does not move");
    Require(LW::ReunionOpen(life, 112.7f) && !LW::ReunionOpen(life, 112.8f), "and theirs to tell for six game hours");
    LW::Life none;
    LW::ReunionTold(none, 5.0f);
    Require(!LW::Has(none, LW::kReunionTold), "no reunion, nothing to greet");
}

static void Past() {
    LW::Life life = Uthgerd();
    for (int i = 0; i < 6; ++i) LW::Remember(life, Step(LW::Activity::Working, 10, 100.0f + static_cast<float>(i)));
    Require(life.pastCount == LW::kPastSteps && life.past[0].at == 102.0f && life.past[LW::kPastSteps - 1].at == 105.0f,
            "the last four steps are kept, the oldest go");
    Require(LW::LastStep(life) && LW::LastStep(life)->at == 105.0f, "the newest is the last");
    LW::ForgetPast(life);
    Require(life.pastCount == 0 && !LW::LastStep(life) && life.past[0].at == 0.0f, "a new parting, a new story");
}

static void Endings() {
    int finds = 0, hurts = 0, kept = 0;
    for (std::uint32_t seed = 0; seed < 2000; ++seed) {
        const auto o = LW::ComeBack(seed * 2654435761u + 7u);
        if (o.find) {
            ++finds;
            Require(o.gold >= 40 && o.gold < 200, "a find comes with some gold");
        } else {
            Require(o.gold == 0 && !o.keepsake, "nothing found: no gold, nothing kept");
        }
        if (o.hurt) ++hurts;
        if (o.keepsake) ++kept;
        Require(o.wound < LW::kWoundCount, "a wound there are words for");
        const auto again = LW::ComeBack(seed * 2654435761u + 7u);
        Require(again.find == o.find && again.hurt == o.hurt && again.gem == o.gem && again.gold == o.gold, "the same save ends it the same way");
    }
    Require(finds > 1000 && finds < 1400, "more often than not something is found");
    Require(hurts > 500 && hurts < 900, "hurt about one time in three");
    Require(kept > finds / 3 && kept < finds * 3 / 5, "a little under half the finds are kept for the player");
    const auto all = LW::ComeBack(1, 7), none = LW::ComeBack(1, 0), keptOnly = LW::ComeBack(1, 4);
    Require(all.find && all.hurt && all.keepsake && !none.find && !none.hurt && !none.keepsake, "the test hook's outcomes");
    Require(!keptOnly.keepsake, "nothing kept without a find");
}

static void Words() {
    const auto n = Fake();
    LW::Life life = Uthgerd();
    LW::Remember(life, Step(LW::Activity::Working, 11, 107.0f));
    LW::PastStep adventure = Step(LW::Activity::Adventuring, 20, 110.0f, 2);
    adventure.over = true;
    adventure.back.find = true;
    adventure.back.hurt = true;
    adventure.back.gold = 120;
    adventure.back.wound = 1;
    adventure.find = 0x63B42;
    LW::Remember(life, adventure);
    LW::Remember(life, Step(LW::Activity::Working, 10, 113.0f));
    life.metAgainAt = 113.5f;
    life.apartDays = 13.5f;

    const auto narration = LW::ReunionNarration(life, n);
    Clean(narration, "the reunion's narration");
    Require(narration.starts_with("Uthgerd catches sight of Albrecht for the first time since the two parted ways at Whiterun, 13 days ago"),
            "how long it has been, since the parting");
    Require(Has(narration, "took work at the Sleeping Giant Inn in Riverwood") &&
                Has(narration, "went with Lydia to clear out Fellglow Keep and came back bandaged and sore, but with a Ruby and a purse of gold") &&
                Has(narration, "and took work at the Bannered Mare in Whiterun."),
            "what they did in that time, in order");
    Require(!Has(narration, "cracked ribs") && !Has(narration, "120"), "the narration says what anyone could see");

    const auto block = LW::ReunionBlock(life, n);
    Clean(block, "the reunion's prompt");
    Require(block.starts_with("## Meeting Albrecht again\nI had not seen Albrecht for 13 days, not since we parted ways at Whiterun. What I did in that time:\n"),
            "their own view: how long, since when");
    Require(Has(block, "- I found work at the Sleeping Giant Inn in Riverwood to earn my keep.\n") &&
                Has(block, "- I went with Lydia to clear out Fellglow Keep and came back with a Ruby and 120 gold to show for it, and with a few cracked ribs.\n"),
            "first person, with what they found and what it cost them");
    Require(!Has(block, "I kept"), "no keepsake, none mentioned");
    Require(Has(block, "Seeing Albrecht again, I tell of it in my own words"), "and that they bring it up themselves");

    life.keepsake = 0x63B42;
    life.keepsakeFrom = 20;
    Require(Has(LW::ReunionBlock(life, n), "I kept a Ruby from Fellglow Keep for Albrecht, and mean to put it in Albrecht's hand myself."),
            "a keepsake they still carry is theirs to mention");
    const auto hand = LW::KeepsakeNarration(life, n);
    Clean(hand, "the keepsake's narration");
    Require(hand == "Uthgerd hands Albrecht the Ruby Uthgerd brought back from Fellglow Keep and kept for Albrecht.", "the keepsake changes hands");
    Require(LW::KeepsakeMemory(life, n) == "I gave Albrecht the Ruby I had brought back from Fellglow Keep and kept for Albrecht.", "and is remembered");
    LW::Life nothing = life;
    nothing.keepsake = 0x999;   // an item without a name
    Require(LW::KeepsakeNarration(nothing, n).empty(), "nothing nameless changes hands");

    adventure.back.keepsake = true;
    const auto memory = LW::ReturnMemory(adventure, n);
    Clean(memory, "the memory of coming back");
    Require(memory == "I came back from Fellglow Keep, where I had gone with Lydia, with a Ruby and 120 gold to show for it, and with a few cracked ribs. "
                      "I kept the Ruby for Albrecht, to give when we meet again.",
            "what they remember of coming back");
    LW::PastStep bare = Step(LW::Activity::Adventuring, 20, 110.0f);
    bare.over = true;
    Require(Has(LW::ReturnMemory(bare, n), "unhurt, though with little to show for it"), "and of coming back with nothing");
    bare.back.hurt = true;
    Require(Has(LW::ReturnMemory(bare, n), "with nothing to show for it but a few cracked ribs") ||
                Has(LW::ReturnMemory(bare, n), "with nothing to show for it but a deep cut"),
            "or only hurt");

    // They met in between (a reunion on day 108): what came before is not told again, and it is "the first time in".
    LW::Life later = life;
    later.keepsake = 0;
    later.metAgainAt = 115.0f;
    later.apartDays = 4.0f;
    const auto since = LW::ReunionNarration(later, n);
    Require(Has(since, "Uthgerd catches sight of Albrecht again, for the first time in 4 days") && !Has(since, "Sleeping Giant") &&
                !Has(since, "Fellglow") && Has(since, "took work at the Bannered Mare"),
            "only what passed since they last met");
    // Nothing of their own yet (left behind four days): how long it has been, no more.
    LW::Life fresh = Uthgerd();
    fresh.metAgainAt = 104.0f;
    fresh.apartDays = 4.0f;
    Require(LW::ReunionNarration(fresh, n) == "Uthgerd catches sight of Albrecht for the first time since the two parted ways at Whiterun, 4 days ago.",
            "a short reunion");
    Require(LW::ReunionBlock(fresh, n).starts_with("## Meeting Albrecht again\nI had not seen Albrecht for 4 days, not since we parted ways at Whiterun.\n"),
            "and its prompt");
    LW::Names nobody = n;
    nobody.player.clear();
    Require(LW::ReunionNarration(fresh, nobody).empty() && LW::ReunionBlock(fresh, nobody).empty(), "no name for the player, no words");
    Require(LW::Span(0.5f) == "less than a day" && LW::Span(1.2f) == "a day" && LW::Span(9.0f) == "9 days" && LW::Span(20.0f) == "about 3 weeks" &&
                LW::Span(90.0f) == "about 3 months",
            "how long, in words");
    Require(LW::words::A("Amethyst") == "an Amethyst" && LW::words::A("Ruby") == "a Ruby" && LW::words::A("") == "something", "a or an");
}

static void Keepsakes() {
    const auto n = Fake();
    LW::Life life = Uthgerd();
    Require(!LW::MayHandOver(life, 110.0f) && LW::KeepsakeLine(life, n).empty(), "nothing kept, nothing to hand over");
    life.keepsake = 0x63B42;
    life.keepsakeFrom = 20;
    Require(!LW::MayHandOver(life, 110.0f), "not before the two have met again");
    Require(LW::KeepsakeLine(life, n) == "I kept a Ruby from Fellglow Keep for Albrecht, and mean to put it in Albrecht's hand myself.", "theirs to mention");
    life.flags |= LW::kGreeted;
    Require(LW::MayHandOver(life, 110.0f), "greeted on this step of their life: when they next stand together");
    life.flags = LW::kWasFollower | LW::kReunionTold;
    life.metAgainAt = 110.0f;
    life.apartDays = 5.0f;
    Require(LW::MayHandOver(life, 110.1f) && !LW::MayHandOver(life, 110.5f), "a reunion greeted, while it lasts");
    life.flags = LW::kWasFollower | LW::kWithPlayer;
    Require(LW::MayHandOver(life, 200.0f), "walking with the player again");
    life.flags |= LW::kOnTask;
    Require(!LW::MayHandOver(life, 200.0f), "not while away on an errand");
}

static void Talk() {
    const auto n = Fake();
    auto carries = [](float weight) {
        RU::Rumor r;
        r.weight = weight;
        return RU::Carries(r);
    };
    LW::Told work;
    work.activity = LW::Activity::Working;
    work.place = 11;
    work.firstStep = true;
    auto t = LW::TalkOf(work, n);
    Clean(t.text, "work");
    Require(t.text == "Uthgerd, who once travelled with Albrecht, has been working at the Sleeping Giant Inn in Riverwood for bed and board, folk say." &&
                t.where == 11 && carries(t.weight) == 1,
            "work: hearsay with the inn named, heard in the next hold too");

    LW::Told out;
    out.activity = LW::Activity::Adventuring;
    out.place = 20;
    out.origin = 10;
    t = LW::TalkOf(out, n);
    Clean(t.text, "setting out");
    Require(t.text == "Uthgerd, who once travelled with Albrecht, set out from the Bannered Mare in Whiterun armed for a fight and bound for Fellglow Keep." &&
                t.where == 10 && carries(t.weight) == 1,
            "an adventure: begins where they were seen leaving");
    out.companion = 2;
    Require(Has(LW::TalkOf(out, n).text, "with Lydia, both armed for a fight, and bound for Fellglow Keep"), "with whoever went along");
    out.origin = 0;
    t = LW::TalkOf(out, n);
    Require(t.text == "Uthgerd, who once travelled with Albrecht, was seen on the road with Lydia, armed for a fight and bound for Fellglow Keep." && t.where == 20,
            "nowhere known to have left from: seen on the road");

    LW::PastStep adventure = Step(LW::Activity::Adventuring, 20, 110.0f);
    adventure.over = true;
    adventure.back.find = true;
    adventure.find = 0x63B44;
    adventure.back.gold = 90;
    LW::Told back;
    back.activity = LW::Activity::Working;
    back.place = 10;
    back.back = &adventure;
    t = LW::TalkOf(back, n);
    Clean(t.text, "coming back");
    Require(t.text == "Uthgerd, who once travelled with Albrecht, came back from Fellglow Keep with an Amethyst and a purse of gold to show for it, "
                      "and has taken work at the Bannered Mare in Whiterun since." &&
                t.where == 10 && carries(t.weight) == 3,
            "back with a find: a story that goes round three holds");
    Require(!Has(t.text, "90"), "people saw a purse, not the sum");
    adventure.back.find = false;
    adventure.find = 0;
    adventure.back.gold = 0;
    adventure.back.hurt = true;
    Require(Has(LW::TalkOf(back, n).text, "came back from Fellglow Keep bandaged and sore, with little to show for it") && carries(LW::TalkOf(back, n).weight) == 3,
            "back hurt");
    adventure.back.hurt = false;
    t = LW::TalkOf(back, n);
    Require(Has(t.text, "came back from Fellglow Keep with little to show for it") && carries(t.weight) == 1, "back with nothing: smaller talk");
    back.activity = LW::Activity::Home;
    back.place = 0;
    back.home = 40;
    t = LW::TalkOf(back, n);
    Require(Has(t.text, ", and has gone back to Whiterun.") && t.where == 40, "back and home: begins at home");
    back.home = 0;
    t = LW::TalkOf(back, n);
    Require(Has(t.text, ", and has gone back home.") && t.where == 20, "home unknown: begins by the dungeon");

    LW::Told road;
    road.activity = LW::Activity::Travelling;
    road.place = 30;
    t = LW::TalkOf(road, n);
    Require(t.text == "Uthgerd, who once travelled with Albrecht, was seen on the road to Riften with a traveller's pack." && t.where == 30 && carries(t.weight) == 1,
            "a trip");

    LW::Told home;
    home.activity = LW::Activity::Home;
    home.home = 10;
    home.firstStep = true;
    t = LW::TalkOf(home, n);
    Require(t.text == "Uthgerd, who travelled with Albrecht for a time, has gone back to the Bannered Mare in Whiterun." && t.where == 10 &&
                carries(t.weight) == 1,
            "home: begins at home, heard in the next hold too");
    home.firstStep = false;
    Require(LW::TalkOf(home, n).text == "Uthgerd, who once travelled with Albrecht, has left the road for a while and gone back to the Bannered Mare in Whiterun.",
            "home again after a life of their own");
    home.home = 0;
    Require(LW::TalkOf(home, n).text.empty(), "no home known: nothing to say");
    LW::Told nameless = work;
    nameless.place = 77;
    Require(LW::TalkOf(nameless, n).text.empty(), "a place without a name: nothing to say");
    LW::Names noPlayer = n;
    noPlayer.player.clear();
    Require(LW::TalkOf(work, noPlayer).text.starts_with("Uthgerd has been working at"), "without the player's name the talk still stands");
    for (const auto* text : {"I ", " my ", " me "}) {
        Require(!Has(LW::TalkOf(back, n).text, text) && !Has(LW::TalkOf(out, n).text, text), "talk is third person");
    }
}

static void Ledger() {
    // The talk of a step goes round as the rumor ledger has it: from where it was seen to the holds around, never
    // passed on by the one it is about, nor brought up while they stand within hearing.
    RU::Ledger l;
    RU::Rumor r;
    r.kind = RU::Kind::Life;
    r.home = RU::Whiterun;
    r.about = 0x91918;
    r.owner = RU::Owner::Life;
    r.ownerId = 0x91918;
    r.weight = 0.78f;
    r.text = "Uthgerd, who once travelled with Albrecht, came back from Fellglow Keep with an Amethyst and a purse of gold to show for it.";
    const auto id = l.Add(r, 10.0f);
    for (float day = 11.0f; day < 17.0f; day += 1.0f) l.Tick(day);
    int holds = 0;
    for (std::size_t h = 0; h < RU::kHolds; ++h) if (l.Find(id)->heardIn & RU::Bit(static_cast<std::uint8_t>(h))) ++holds;
    Require(holds >= 3 && holds <= 4, "word of a former follower's adventure reaches the holds around");
    std::uint8_t elsewhere = RU::NoHold;
    for (std::uint8_t h = 0; h < RU::kHolds; ++h) {
        if (h != RU::Whiterun && (l.Find(id)->heardIn & RU::Bit(h))) elsewhere = h;
    }
    Require(elsewhere != RU::NoHold && !l.ForHold(elsewhere, 0x1A66E, 16.0f, 4).empty(), "a publican in another hold has heard it");
    Require(l.ForHold(RU::Whiterun, 0x91918, 16.0f, 4).empty(), "never passed on by the one it is about");
    RU::Ledger fresh;
    const auto told = fresh.Add(r, 10.0f);
    const std::uint32_t here[] = {0x91918};
    Require(!fresh.ToBringUp(RU::Whiterun, 0x1A66E, 10.5f, here), "not brought up across the room they stand in");
    Require(fresh.ToBringUp(RU::Whiterun, 0x1A66E, 10.5f) && fresh.ToBringUp(RU::Whiterun, 0x1A66E, 10.5f)->id == told, "brought up when they are not there");
    fresh.Withdraw(RU::Owner::Life, 0x91918);
    Require(fresh.all.empty(), "their next step replaces it");
    const float lasts = RU::LifeDays(*l.Find(id));
    Require(lasts > 7.0f && lasts < 12.0f, "and it is forgotten in a week or two");
}

static void Record() {
    std::vector<LW::Life> lives{Uthgerd(), Uthgerd()};
    lives[1].actor = 2;
    lives[0].metAgainAt = 112.5f;
    lives[0].apartDays = 12.5f;
    lives[0].keepsake = 0x63B42;
    lives[0].keepsakeFrom = 20;
    lives[0].flags |= LW::kReunionTold;
    LW::Remember(lives[0], Step(LW::Activity::Working, 11, 107.0f));
    LW::PastStep adventure = Step(LW::Activity::Adventuring, 20, 110.0f, 2);
    adventure.over = true;
    adventure.back.find = true;
    adventure.back.keepsake = true;
    adventure.back.hurt = true;
    adventure.back.wound = 2;
    adventure.back.gem = 77;
    adventure.back.gold = 150;
    adventure.find = 0x63B42;
    LW::Remember(lives[0], adventure);
    LW::DirectorState d;
    d.picks = 3;
    const auto bytes = LW::Serialize(lives, d);
    std::vector<LW::Life> back;
    LW::DirectorState dBack;
    Require(LW::Deserialize(bytes, back, dBack), "version 3 round trip");
    const auto& b = back[0];
    Require(back.size() == 2 && b.metAgainAt == 112.5f && b.apartDays == 12.5f && b.keepsake == 0x63B42 && b.keepsakeFrom == 20 &&
                LW::Has(b, LW::kReunionTold) && b.pastCount == 2 && b.past[0].activity == LW::Activity::Working && b.past[0].place == 11 &&
                b.past[1].over && b.past[1].companion == 2 && b.past[1].back.find && b.past[1].back.keepsake && b.past[1].back.hurt &&
                b.past[1].back.wound == 2 && b.past[1].back.gem == 77 && b.past[1].back.gold == 150 && b.past[1].find == 0x63B42 && back[1].pastCount == 0 &&
                dBack.picks == 3,
            "everything of what passed comes back");
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        std::vector<LW::Life> none{LW::Life{}};
        LW::DirectorState dNone;
        Require(!LW::Deserialize(std::span(bytes.data(), cut), none, dNone) && none.size() == 1, "short data is refused whole");
    }
    auto longer = bytes;
    longer.push_back(0);
    Require(!LW::Deserialize(longer, back, dBack), "trailing bytes are refused");
    // A version 2 record (a save from before) still loads, with nothing of the kind.
    const auto old = LW::Serialize(lives, d, 2);
    Require(old.size() < bytes.size(), "version 2 is the record without what passed");
    std::vector<LW::Life> fromOld;
    Require(LW::Deserialize(old, fromOld, dBack, 2) && fromOld.size() == 2 && fromOld[0].pastCount == 0 && fromOld[0].keepsake == 0 &&
                fromOld[0].metAgainAt < 0.0f && fromOld[0].parkedSince == 100.0f,
            "a version 2 save loads");
    Require(!LW::Deserialize(old, fromOld, dBack, 3) && !LW::Deserialize(bytes, fromOld, dBack, 2), "each version reads only its own");
    Require(!LW::Deserialize(bytes, fromOld, dBack, 4) && !LW::Deserialize(bytes, fromOld, dBack, 1), "an unknown version is refused");
    {
        // A count of past steps beyond what is kept: refused.
        std::vector<LW::Life> one{Uthgerd()};
        auto raw = LW::Serialize(one, d);
        // The last byte of a life without steps is its step count (after metAgainAt, apartDays, keepsake, keepsakeFrom).
        raw.back() = static_cast<std::uint8_t>(LW::kPastSteps + 1);
        std::vector<LW::Life> none;
        Require(!LW::Deserialize(raw, none, dBack), "too many steps are refused");
    }
}

int main() {
    try {
        Reunions();
        Past();
        Endings();
        Words();
        Keepsakes();
        Talk();
        Ledger();
        Record();
    } catch (const std::exception& e) {
        std::cerr << "LivesToldCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "LivesToldCheck passed\n";
    return 0;
}
