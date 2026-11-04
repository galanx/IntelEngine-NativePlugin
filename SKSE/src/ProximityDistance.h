#pragma once
#include <algorithm>
#include <cmath>

namespace IntelEngine {
// ponytail: 150ms polling + estimated 350ms VM/package delay; calibrate from live timing if NPCs still overshoot.
inline constexpr float PROXIMITY_EXPECTED_LATENCY_SECONDS = 0.50f;
inline float GetActorArrivalThreshold(float baseThreshold, float speed, float acceptedThreshold = 0.0f) {
    return std::max(baseThreshold + std::max(speed, 0.0f) * PROXIMITY_EXPECTED_LATENCY_SECONDS,
                    acceptedThreshold);
}
inline bool IsWithinProximityDistance(float dx, float dy, float dz, float threshold,
    float zTolerance, bool actorTarget) {
    return std::fabs(dz) <= zTolerance &&
        dx * dx + dy * dy + (actorTarget ? dz * dz : 0.0f) <= threshold * threshold;
}
}  // namespace IntelEngine
