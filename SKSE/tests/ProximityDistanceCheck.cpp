#include "ProximityDistance.h"
#include <iostream>
#include <stdexcept>

static void Require(bool value) { if (!value) throw std::runtime_error("proximity distance mismatch"); }
int main() {
    using IntelEngine::IsWithinProximityDistance;
    Require(IsWithinProximityDistance(200, 0, 0, 200, 120, true));
    Require(!IsWithinProximityDistance(200.1f, 0, 0, 200, 120, true));
    Require(!IsWithinProximityDistance(190, 0, 100, 200, 120, true));
    Require(IsWithinProximityDistance(160, 0, 120, 200, 120, true));
    Require(!IsWithinProximityDistance(0, 0, 121, 200, 120, true));
    Require(IsWithinProximityDistance(300, 0, 200, 300, 200, false));
    Require(!IsWithinProximityDistance(300.1f, 0, 0, 300, 200, false));
    Require(!IsWithinProximityDistance(0, 0, 201, 300, 200, false));
    std::cout << "PASS: actor radial/Z boundaries, consumed-callback regression and unchanged marker cylinder\n";
}
