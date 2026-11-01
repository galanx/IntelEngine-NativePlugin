#pragma once
#include <cmath>

namespace IntelEngine {
inline bool IsWithinProximityDistance(float dx, float dy, float dz, float threshold,
    float zTolerance, bool actorTarget) {
    return std::fabs(dz) <= zTolerance &&
        dx * dx + dy * dy + (actorTarget ? dz * dz : 0.0f) <= threshold * threshold;
}
}  // namespace IntelEngine
