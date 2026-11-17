// Standalone C++20 check; no game ABI or test framework.
// From a VS developer shell: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/LivingWorldCheck.cpp
#include "LivingWorld.h"

#include <iostream>
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
    Situation dungeon{Seam::InDungeon, 31.0f, false};
    Require(!MayDeliverNow(d, s, dungeon, false, false, 1), "deep in a dungeon only urgent things come");
    Require(MayDeliverNow(d, s, dungeon, true, false, 1), "help can still arrive");
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

static void SaveData() {
    std::vector<Life> lives{Person(1, 0.9f, 10.0f, kWasFollower | kWaitOrder), Person(2, 0.1f, 3.0f)};
    lives[0].activity = Activity::Adventuring;
    lives[0].companion = 2;
    lives[0].parkedSince = 5.0f;
    DirectorState d;
    d.picks = 7;
    d.lastActor = 2;
    const auto bytes = Serialize(lives, d);
    std::vector<Life> back;
    DirectorState dBack;
    Require(Deserialize(bytes, back, dBack), "round trip");
    Require(back.size() == 2 && back[0].activity == Activity::Adventuring && back[0].companion == 2 &&
                back[0].parkedSince == 5.0f && back[1].bond == 0.1f && dBack.picks == 7 && dBack.lastActor == 2,
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
        SaveData();
    } catch (const std::exception& e) {
        std::cerr << "LivingWorldCheck FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "LivingWorldCheck passed\n";
    return 0;
}
