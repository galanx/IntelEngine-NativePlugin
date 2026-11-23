#pragma once

// 4.0: travel to places named in SkyrimNet: the player's (chat /mark, the Places page), a content pack's, and other
// mods' travel markers that they keep in SkyrimNet (SeverActions'). A SkyrimNet place is a position in a cell or
// worldspace, not a reference, so this finds the place and a persistent reference in the same space (a door inside,
// the nearest location marker outside, or the player or the NPC when they are there); the scripts move the NPC's
// own marker to it with the offset (IntelEngine_Core.ResolveTravelTarget) and the trip goes to that marker.

#include "PlaceNames.h"

#include <optional>
#include <string>

namespace IntelEngine::PlaceTargets {

    struct Target {
        RE::TESObjectREFR* anchor = nullptr;
        RE::NiPoint3 offset{};  // from the anchor to the place
        std::string name;
    };

    /** The SkyrimNet place a destination names, or none. A room word ("the kitchen", "upstairs") matches only a
     *  place in the NPC's own cell; any other name matches anywhere, the NPC's own space first, then the player's,
     *  then the nearest. "Kitchen in Breezehome" and "Breezehome kitchen" name a place with its location. */
    std::optional<Target> Resolve(RE::Actor* npc, const std::string& destination);
}
