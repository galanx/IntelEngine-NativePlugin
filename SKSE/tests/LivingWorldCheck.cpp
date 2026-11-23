// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/LivingWorldCheck.cpp
#include "LivingWorld.h"

#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>

using namespace IntelEngine::LivingWorld;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static Life Person(std::uint32_t actor, float bond, float lastMet, std::uint16_t flags = 0) {
    Life life;
    life.actor = actor;
    life.bond = bond;
    life.lastMet = lastMet;
    life.flags = flags;
    return life;
}

static void Truth() {
    Settings s;
    const Life lydiaWithMe = Person(1, 0.9f, 100.0f, kWithPlayer | kWasFollower);
    Require(PinnedByReality(lydiaWithMe), "a follower at the player's side is pinned");
    Require(!MayRelocate(lydiaWithMe, s, true), "nobody may say Lydia is in Riften while she walks with the player");
    Life lydiaParked = Person(1, 0.9f, 100.0f, kWasFollower);
    lydiaParked.parkedSince = 100.0f;
    Require(!LifeMayBegin(lydiaParked, s, 106.9f), "no life of her own before 7 days apart");
    Require(LifeMayBegin(lydiaParked, s, 107.0f), "a life of her own after 7 days apart");
    lydiaParked.flags |= kWaitOrder;
    Require(!LifeMayBegin(lydiaParked, s, 129.0f) && LifeMayBegin(lydiaParked, s, 130.0f), "a wait order holds 30 days");
    lydiaParked.flags |= kNearPlayer;
    Require(!LifeMayBegin(lydiaParked, s, 200.0f), "nothing changes while the player is around");
    Settings rumors = s;
    rumors.level = WorldLevel::Rumors;
    lydiaParked.flags = kWasFollower;
    Require(!LifeMayBegin(lydiaParked, rumors, 200.0f), "followers keep still below the Lives level");
    Settings classic = s;
    classic.level = WorldLevel::Classic;
    Require(!MayRelocate(Person(2, 0.5f, 0.0f), classic, true), "Classic moves nobody");

    const Life onTask = Person(3, 0.5f, 0.0f, kOnTask);
    Require(!MayRelocate(onTask, s, true), "an order in progress is the truth");
    const Life shop = Person(4, 0.5f, 0.0f, kKeepsShop);
    Require(!MayRelocate(shop, s, true), "a merchant keeps the shop open");
    const Life questTarget = Person(5, 0.5f, 0.0f, kQuestTarget);
    Settings freely = s;
    freely.questNpcMoves = QuestNpcMoves::Freely;
    Require(!MayRelocate(questTarget, freely, true), "an open objective's target never moves");
    const Life questBound = Person(6, 0.5f, 0.0f, kQuestBound);
    Require(MayRelocate(questBound, s, true) && !MayRelocate(questBound, s, false), "quest NPCs move for important things only");
    Settings never = s;
    never.questNpcMoves = QuestNpcMoves::Never;
    Require(!MayRelocate(questBound, never, true), "the setting can forbid it");
    Require(MayRelocate(questBound, freely, false), "or allow it freely");
}

static void Followers() {
    const std::vector<std::uint32_t> before{1, 2, 3}, now{2}, tasks{3};
    const auto left = LeftBehind(before, now, tasks);
    Require(left.size() == 1 && left[0] == 1, "only the one who stopped following without a task was left behind");
}

static void PacingRules() {
    Settings s;
    DirectorState d;
    d.budget = 0.0f;
    d.budgetDay = 10.0f;
    Situation town{Seam::ArrivedInTown, 10.2f, false};
    Require(!MayDeliverNow(d, s, town, false, false, 1), "no budget, no ordinary moment");
    Require(MayDeliverNow(d, s, town, true, false, 1), "urgent goes through");
    Refill(d, s, 11.0f);
    town.now = 11.0f;
    Require(d.budget >= 1.0f && MayDeliverNow(d, s, town, false, false, 1), "one moment a day at Normal pacing");
    Refill(d, s, 30.0f);
    Require(d.budget <= kMaxBudget, "the budget never piles up");
    d.lastMomentDay = 30.95f;
    Situation soon{Seam::EnteredInn, 31.0f, false};
    Require(!MayDeliverNow(d, s, soon, false, false, 1), "not twice within three game hours");
    d.lastMomentDay = 0.0f;
    d.lastBigDay = 30.5f;
    Require(!MayDeliverNow(d, s, soon, false, true, 1), "a rest after a big moment");
    Require(MayDeliverNow(d, s, soon, false, false, 1), "a small one is fine");
    // Skipping time does not bring the next moment: it takes time actually played as well.
    Situation skipped{Seam::Woke, 40.0f, false};
    DirectorState rested;
    rested.budget = 2.0f;
    rested.budgetDay = 40.0f;
    rested.lastMomentDay = 39.0f;            // a moment a game day ago...
    skipped.playedSince = 30.0f;             // ...which was half a minute of play ago: the player waited a day
    Require(!MayDeliverNow(rested, s, skipped, false, false, 1), "a day waited through is no time played: no moment yet");
    Require(MayDeliverNow(rested, s, skipped, true, false, 1), "something urgent comes all the same");
    Settings counting = s;
    counting.skippedTimeCounts = true;
    Require(MayDeliverNow(rested, counting, skipped, false, false, 1),
            "with \"Skipped time counts toward the pace\" on, the game's clock alone counts: the day waited through is a day");
    skipped.playedSince = kMinGapPlayedSeconds;
    Require(MayDeliverNow(rested, s, skipped, false, false, 1), "after eight minutes of play it may");
    skipped.playedSince = -1.0f;
    Require(MayDeliverNow(rested, s, skipped, false, false, 1), "no moment yet this session: nothing to wait out");
    Situation dungeon{Seam::InDungeon, 31.0f, false};
    Require(!MayDeliverNow(d, s, dungeon, false, false, 1), "deep in a dungeon only urgent things come");
    Require(MayDeliverNow(d, s, dungeon, true, false, 1), "help can still arrive");
    // The bridge refills up to the moment before counting it, so the cap applies at the moment and the time
    // before it is not credited a second time afterwards (1.9 banked, a moment half a day later: 2 - 1, then +0.5).
    DirectorState banked;
    banked.budget = 1.9f;
    banked.budgetDay = 10.0f;
    Life someone = Person(30, 0.5f, 0.0f);
    Candidate moment{&someone};
    Refill(banked, s, 10.5f);
    Commit(banked, someone, moment, {Seam::EnteredInn, 10.5f, false});
    Refill(banked, s, 11.0f);
    Require(banked.budget > 1.49f && banked.budget < 1.51f, "refilled up to the moment, then counted, then refilled again");
    Situation scene{Seam::EnteredInn, 31.0f, true};
    Require(!MayDeliverNow(d, s, scene, false, false, 1), "Only urgent: a bad moment holds ordinary things back");
    Require(MayDeliverNow(d, s, scene, true, false, 1), "Only urgent: urgent things interrupt");
    Settings never = s;
    never.interruptions = Interruptions::Never;
    Require(!MayDeliverNow(d, never, scene, true, false, 1), "Never: not even urgent things interrupt");
    Settings realistic = s;
    realistic.interruptions = Interruptions::Realistic;
    int through = 0;
    for (std::uint32_t seed = 0; seed < 100; ++seed) through += MayDeliverNow(d, realistic, scene, false, false, seed) ? 1 : 0;
    Require(through == 25, "Realistic: about one ordinary moment in four interrupts");
}

static void Picking() {
    DirectorState d;
    const Situation home{Seam::CameHome, 100.0f, false};
    const Life close = Person(10, 0.9f, 80.0f);           // a close friend, not seen for 20 days
    const Life acquaintance = Person(11, 0.2f, 60.0f);    // met once, 40 days ago
    const Life recent = Person(12, 0.9f, 99.5f);          // seen this morning
    std::vector<Candidate> c{{&close}, {&acquaintance}, {&recent}};
    auto pick = Pick(c, d, home, 7);
    Require(pick && *pick == 0, "a close friend missed for long comes first");
    d.lastActor = 10;
    pick = Pick(c, d, home, 7);
    Require(pick && *pick != 0, "never the same person twice running");
    d.lastActor = 0;
    d.picks = kRotationEvery - 1;
    pick = Pick(c, d, home, 7);
    Require(pick && *pick == 1, "every fourth moment goes to a forgotten acquaintance");

    const Life withMe = Person(13, 1.0f, 0.0f, kWithPlayer);
    const Life working = Person(14, 1.0f, 0.0f, kOnTask);
    const Life nearby = Person(15, 1.0f, 0.0f, kNearPlayer);
    Candidate nearbyElsewhere{&nearby};
    Candidate nearbyHere{&nearby};
    nearbyHere.here = true;
    Require(!Eligible(Candidate{&withMe}) && !Eligible(Candidate{&working}), "no elsewhere-moments for companions or workers");
    Require(!Eligible(nearbyElsewhere) && Eligible(nearbyHere), "someone nearby only gets a moment right here");

    d.picks = 0;
    std::vector<Candidate> nobody{{&recent}};
    d.lastActor = 12;
    Require(!Pick(nobody, d, home, 7), "nothing at all beats a forced, pointless moment");
}

static void Deliveries() {
    const Life friendFar = Person(20, 0.8f, 70.0f);
    Candidate c{&friendFar};
    c.tripDays = 3.0f;
    Require(ChooseDelivery(c, {Seam::CameHome, 100.0f, false}) == Delivery::Letter, "too far for a visit: a letter");
    c.tripDays = 1.0f;
    Require(ChooseDelivery(c, {Seam::CameHome, 100.0f, false}) == Delivery::Visit, "a close friend missing you comes over");
    Require(ChooseDelivery(c, {Seam::EnteredInn, 100.0f, false}) == Delivery::Rumor, "in the inn you hear about it");
    c.onRoute = 0.8f;
    Require(ChooseDelivery(c, {Seam::OnRoad, 100.0f, false}) == Delivery::ChanceMeeting, "on the road you run into them");
    c.here = true;
    Require(ChooseDelivery(c, {Seam::EnteredInn, 100.0f, false}) == Delivery::WaitingThere, "someone here waits for you");
    c.here = false;
    c.onRoute = 0.0f;
    Require(ChooseDelivery(c, {Seam::InCombat, 100.0f, false}) == Delivery::None, "nothing ordinary in a fight");
}

static void Seams() {
    const PlaceKind wild{}, dungeon{true, false, false, false}, inn{false, true, true, false}, town{false, false, true, false},
        home{false, false, true, true};
    Require(ClassifySeam(wild, town) == Seam::ArrivedInTown, "arriving in town");
    Require(ClassifySeam(town, inn) == Seam::EnteredInn, "walking into the inn");
    Require(ClassifySeam(inn, town) == Seam::OnRoad, "stepping out of the inn is no new arrival");
    Require(ClassifySeam(dungeon, wild) == Seam::LeftDungeon, "out of the dungeon");
    Require(ClassifySeam(wild, dungeon) == Seam::InDungeon, "into the dungeon");
    Require(ClassifySeam(town, home) == Seam::CameHome, "coming home");
    Require(ClassifySeam(home, home) != Seam::CameHome, "moving between rooms at home is not coming home");
}

static void Lives() {
    const std::vector<PlaceOption> inns{{100, 1}, {101, 2}}, dungeons{{200, 1}, {201, 2}}, towns{{300, 1}};
    Life lydia = Person(1, 0.9f, 0.0f, kWasFollower);
    lydia.parkedSince = 0.0f;
    int working = 0, adventuring = 0, travelling = 0, home = 0;
    for (std::uint32_t seed = 0; seed < 400; ++seed) {
        const auto step = NextStep(lydia, 1, inns, dungeons, towns, {}, 10.0f, seed);
        Require(step.until >= 11.0f && step.until <= 20.0f, "a step lasts days");
        if (step.activity == Activity::Working) {
            ++working;
            Require(step.place == 100, "work at an inn of her own hold");
        } else if (step.activity == Activity::Adventuring) {
            ++adventuring;
            Require(step.place == 200, "an adventure at a dungeon of her hold");
        } else if (step.activity == Activity::Travelling) {
            ++travelling;
            Require(step.place == 300, "a trip to a real town");
        } else {
            ++home;
            Require(step.place == 0, "home is her own home");
        }
    }
    Require(working > adventuring && adventuring > travelling && travelling > home && home > 0, "work most, home least");
    const auto elsewhere = NextStep(lydia, 7, inns, dungeons, towns, {}, 10.0f, 0);
    Require(elsewhere.activity == Activity::Home || elsewhere.place != 0, "a hold without places of a kind uses any");

    lydia.activity = Activity::Working;
    lydia.place = 100;
    for (std::uint32_t seed = 0; seed < 400; ++seed) {
        const auto step = NextStep(lydia, 1, inns, dungeons, towns, {}, 10.0f, seed);
        Require(step.activity != Activity::Working || step.place != 100, "moving on is never to the same inn");
    }
    lydia.activity = Activity::Adventuring;
    lydia.place = 200;
    for (std::uint32_t seed = 0; seed < 400; ++seed) {
        Require(NextStep(lydia, 1, inns, dungeons, towns, {}, 10.0f, seed).activity != Activity::Adventuring,
                "back from an adventure: never straight into another");
    }
    const auto nowhere = NextStep(lydia, 1, {}, {}, {}, {}, 10.0f, 3);
    Require(nowhere.activity == Activity::Home && nowhere.place == 0, "no places in the load order: home");

    Life uthgerd = Person(2, 0.5f, 0.0f, kWasFollower);
    uthgerd.parkedSince = 0.0f;
    uthgerd.activity = Activity::Working;
    lydia.activity = Activity::Home;
    lydia.place = 0;
    const std::vector<Life> others{uthgerd, lydia};
    int together = 0;
    for (std::uint32_t seed = 0; seed < 400; ++seed) {
        const auto step = NextStep(lydia, 1, inns, dungeons, towns, others, 10.0f, seed);
        if (step.companion != 0) {
            Require(step.activity == Activity::Adventuring && step.companion == 2, "only an adventure takes a companion, never herself");
            ++together;
        }
    }
    Require(together > 0, "now and then someone free goes along");
    for (const std::uint16_t busyFlag : {static_cast<std::uint16_t>(kWithPlayer), static_cast<std::uint16_t>(kQuestBound),
                                         static_cast<std::uint16_t>(kKeepsShop), static_cast<std::uint16_t>(kQuestTarget)}) {
        Life busy = uthgerd;
        busy.flags |= busyFlag;
        const std::vector<Life> list{busy};
        for (std::uint32_t seed = 0; seed < 200; ++seed) {
            Require(NextStep(lydia, 1, inns, dungeons, towns, list, 10.0f, seed).companion == 0,
                    "nobody bound elsewhere goes along");
        }
    }

    Require(std::string(StepMemory(Activity::Working, true, false)).find("{parked}") != std::string::npos,
            "the first step remembers where they parted");
    for (const auto activity : {Activity::Working, Activity::Adventuring, Activity::Travelling, Activity::Home}) {
        Require(std::string(StepMemory(activity, true, false)).starts_with("After {player} and I parted ways at {parked}, "),
                "every first step remembers where they parted");
        Require(std::string(StepMemory(activity, false, false)).find("{parked}") == std::string::npos,
                "later steps are the step alone");
    }
    Require(std::string(StepRumor(Activity::Home, false)).empty(), "going home is no rumor");
    Require(Ago(0.3f) == "today" && Ago(1.5f) == "a day ago" && Ago(5.2f) == "5 days ago" && Ago(20.0f) == "about 3 weeks ago" &&
                Ago(90.0f) == "about 3 months ago",
            "how long ago, in words");

    Life life = Person(3, 0.8f, 0.0f, kWasFollower);
    Require(std::string(OwnLifeLine(life)).empty(), "never left behind: nothing to tell");
    life.flags |= kWithPlayer;
    Require(std::string(OwnLifeLine(life)).empty(), "following: nothing to tell");
    life.flags |= kWaitOrder;
    life.parkedSince = 2.0f;
    Require(std::string(OwnLifeLine(life)).find("waiting for {player}") != std::string::npos, "told to wait: they know it");
    life.flags = kWasFollower;
    Require(std::string(OwnLifeLine(life)).empty(), "left behind but no step yet: vanilla, nothing to tell");
    life.steps = 1;
    life.activity = Activity::Adventuring;
    life.companion = 2;
    Require(std::string(OwnLifeLine(life)).find("with {companion}") != std::string::npos, "an adventure names who came");
    Require(std::string(DirectorLifeLine(life)).find("{player}") != std::string::npos &&
                std::string(DirectorLifeLine(life)).find("I ") == std::string::npos,
            "the director's line is third person");
    Require(OwnLife(life) && !OwnLife(Person(4, 0.1f, 0.0f)), "a life of their own");
}

static void SaveData() {
    std::vector<Life> lives{Person(1, 0.9f, 10.0f, kWasFollower | kWaitOrder), Person(2, 0.1f, 3.0f)};
    lives[0].activity = Activity::Adventuring;
    lives[0].companion = 2;
    lives[0].parkedSince = 5.0f;
    lives[0].rumor = 4242;
    lives[0].steps = 3;
    lives[0].stepAt = 8.5f;
    DirectorState d;
    d.picks = 7;
    d.lastActor = 2;
    const auto bytes = Serialize(lives, d);
    std::vector<Life> back;
    DirectorState dBack;
    Require(Deserialize(bytes, back, dBack), "round trip");
    Require(back.size() == 2 && back[0].activity == Activity::Adventuring && back[0].companion == 2 &&
                back[0].parkedSince == 5.0f && back[0].rumor == 4242 && back[0].steps == 3 && back[0].stepAt == 8.5f &&
                back[1].bond == 0.1f && dBack.picks == 7 && dBack.lastActor == 2,
            "everything comes back");
    std::vector<Life> untouched{Person(9, 0.5f, 1.0f)};
    DirectorState dUntouched;
    dUntouched.picks = 99;
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        Require(!Deserialize(std::span(bytes.data(), cut), untouched, dUntouched), "short data is refused");
    }
    Require(untouched.size() == 1 && untouched[0].actor == 9 && dUntouched.picks == 99, "and changes nothing");
    auto longer = bytes;
    longer.push_back(0);
    Require(!Deserialize(longer, untouched, dUntouched), "trailing bytes are refused");
    std::vector<std::uint8_t> tooMany(4, 0xFF);
    Require(!Deserialize(tooMany, untouched, dUntouched), "an absurd count is refused");

    std::vector<Life> many;
    for (std::uint32_t i = 0; i < 80; ++i) many.push_back(Person(100 + i, i == 5 ? 0.0f : 0.5f, static_cast<float>(i), i == 5 ? kWasFollower : 0));
    Trim(many);
    Require(many.size() == kMaxLives && many[0].actor == 105, "trimming keeps former followers first");
}

int main() {
    try {
        Truth();
        Followers();
        PacingRules();
        Picking();
        Deliveries();
        Seams();
        Lives();
        SaveData();
    } catch (const std::exception& e) {
        std::cerr << "LivingWorldCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "LivingWorldCheck passed\n";
    return 0;
}
