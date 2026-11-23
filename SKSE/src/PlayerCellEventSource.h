#pragma once

#include <cstddef>
#include <cstdint>

// Where PlayerCharacter's BSTEventSource<BGSActorCellEvent> base sits on each game version.
// CommonLib's AsBGSActorCellEventSource() says 0x2E8 on 1.6.629 and later, but the game's own PlayerCharacter
// destructor tears that source down at 0x2D8 on 1.6.1170 and at 0x2E0 on 1.7.104 (1.7.99 added a BSTEventSink
// base at 0x2D8). Registering at 0x2E8 writes into the middle of the source; on 1.7 it wrote into SkyrimSE.exe
// and crashed the game at start-up. SE before 1.6.629 and VR keep CommonLib's 0x2D0 and 0x2D8.
// ponytail: only this source is corrected; the death and position-player sources have the same 0x10 error in
// CommonLib but IntelEngine does not use them.
inline constexpr std::ptrdiff_t PlayerCellEventSourceOffset(bool vr, std::uint16_t major, std::uint16_t minor, std::uint16_t patch) {
    if (vr) return 0x2D8;
    const auto version = (std::uint64_t{major} << 32) | (std::uint64_t{minor} << 16) | patch;
    constexpr auto v1_6_629 = (std::uint64_t{1} << 32) | (std::uint64_t{6} << 16) | 629;
    constexpr auto v1_7_99 = (std::uint64_t{1} << 32) | (std::uint64_t{7} << 16) | 99;
    if (version >= v1_7_99) return 0x2E0;
    if (version >= v1_6_629) return 0x2D8;
    return 0x2D0;
}
