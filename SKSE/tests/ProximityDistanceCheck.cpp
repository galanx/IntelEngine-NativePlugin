#include "ProximityDistance.h"
#include <iostream>
#include <stdexcept>

static void Require(bool value) { if (!value) throw std::runtime_error("proximity distance mismatch"); }
int main() {
    using IntelEngine::GetActorArrivalThreshold;
    using IntelEngine::IsWithinProximityDistance;
    Require(GetActorArrivalThreshold(200, 600) == 500);  // 0.50s estimated response budget.
    Require(GetActorArrivalThreshold(200, 0) == 200);
    Require(GetActorArrivalThreshold(200, 0, 400) == 400);  // Claimed callback receipt survives speed dropping before Papyrus recheck.
    Require(IsWithinProximityDistance(350, 0, 0, GetActorArrivalThreshold(200, 0, 400), 120, true));
    Require(!IsWithinProximityDistance(401, 0, 0, GetActorArrivalThreshold(200, 0, 400), 120, true));
    Require(GetActorArrivalThreshold(200, -1) == 200);
    Require(IsWithinProximityDistance(200, 0, 0, 200, 120, true));
    Require(!IsWithinProximityDistance(200.1f, 0, 0, 200, 120, true));
    Require(IsWithinProximityDistance(400, 0, 0, GetActorArrivalThreshold(200, 400), 120, true));
    Require(!IsWithinProximityDistance(401, 0, 0, GetActorArrivalThreshold(200, 400), 120, true));
    Require(!IsWithinProximityDistance(190, 0, 100, 200, 120, true));
    Require(IsWithinProximityDistance(160, 0, 120, 200, 120, true));
    Require(!IsWithinProximityDistance(0, 0, 121, 200, 120, true));
    Require(IsWithinProximityDistance(300, 0, 200, 300, 200, false));
    Require(!IsWithinProximityDistance(300.1f, 0, 0, 300, 200, false));
    Require(!IsWithinProximityDistance(0, 0, 201, 300, 200, false));
    std::cout << "PASS: speed-aware actor threshold, actor radial/Z boundaries and unchanged marker cylinder\n";
}
