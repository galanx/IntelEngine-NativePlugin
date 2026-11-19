#include "PlaceTargets.h"
#include "Safe.h"

#include "LocationResolver.h"
#include "SkyrimNetAPI.h"

#include <nlohmann/json.hpp>

#include <cfloat>

namespace IntelEngine::PlaceTargets {

    namespace {
        RE::FormID SpaceOf(RE::TESObjectREFR* ref) {
            if (!ref) return 0;
            auto* cell = ref->GetParentCell();
            if (cell && cell->IsInteriorCell()) return cell->GetFormID();
            auto* world = ref->GetWorldspace();
            return world ? world->GetFormID() : 0;
        }

        bool Outdoors(RE::TESObjectREFR* ref) {
            auto* cell = ref ? ref->GetParentCell() : nullptr;
            return cell && !cell->IsInteriorCell();
        }

        std::string LocationName(std::uint32_t id) {
            auto* location = id ? RE::TESForm::LookupByID<RE::BGSLocation>(id) : nullptr;
            const char* name = location ? location->GetFullName() : nullptr;
            return name ? name : "";
        }

        // A persistent reference inside an interior that is not loaded: a door (load doors always are).
        RE::TESObjectREFR* DoorIn(RE::TESObjectCELL* cell) {
            RE::TESObjectREFR* door = nullptr;
            RE::TESObjectREFR* any = nullptr;
            cell->ForEachReference([&](RE::TESObjectREFR* ref) -> RE::BSContainer::ForEachResult {
                if (!ref || ref->IsDisabled() || ref->IsDeleted()) return RE::BSContainer::ForEachResult::kContinue;
                auto* base = ref->GetBaseObject();
                if (base && base->Is(RE::FormType::Door) && ref->extraList.GetByType<RE::ExtraTeleport>()) {
                    door = ref;
                    return RE::BSContainer::ForEachResult::kStop;
                }
                // Persistent only: the trip keeps its target as a linked reference, and a temporary one is freed when
                // its cell unloads (the maintainer's finding with his own Named Places, 2026-11-17: a crash in the
                // travel package after the player had left the cell).
                if (!any && !ref->As<RE::Actor>() && ref->IsPersistent()) any = ref;
                return RE::BSContainer::ForEachResult::kContinue;
            });
            return door ? door : any;
        }

        // The location marker nearest to a point of a worldspace (a persistent reference there).
        RE::TESObjectREFR* MarkerNear(RE::TESWorldSpace* world, const RE::NiPoint3& point) {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return nullptr;
            RE::TESObjectREFR* best = nullptr;
            float bestDistance = FLT_MAX;
            for (auto* location : data->GetFormArray<RE::BGSLocation>()) {
                if (!location) continue;
                auto marker = std::as_const(location->worldLocMarker).get();
                if (!marker || marker->GetWorldspace() != world) continue;
                const float distance = marker->GetPosition().GetDistance(point);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = marker.get();
                }
            }
            return best;
        }
    }

    std::optional<Target> Resolve(RE::Actor* npc, const std::string& destination) {
        if (!npc || !SkyrimNetAPI::ListPlaces) return std::nullopt;
        const auto wanted = Fold(destination);
        if (wanted.empty()) return std::nullopt;
        auto* resolver = LocationResolver::GetSingleton();
        const bool roomWord = resolver->IsSemanticTerm(destination) || resolver->IsSemanticTerm(wanted);

        auto places = nlohmann::json::parse(SkyrimNetAPI::ListPlaces(""), nullptr, false);
        if (!places.is_array() || places.empty()) return std::nullopt;

        auto* player = RE::PlayerCharacter::GetSingleton();
        const RE::FormID npcSpace = SpaceOf(npc);
        const RE::FormID playerSpace = SpaceOf(player);
        const auto npcAt = npc->GetPosition();

        struct Found {
            std::string name;
            RE::FormID space = 0;
            RE::NiPoint3 at{};
        };
        std::optional<Found> best;
        int bestRank = 99;
        float bestDistance = FLT_MAX;
        for (const auto& entry : places) {
            if (!entry.is_object()) continue;
            // SkyrimNet's list is another mod's data: a field of the wrong kind reads as nothing (Safe.h), it never throws.
            const auto name = Safe::Text(entry, "name");
            const auto owner = static_cast<std::uint32_t>(Safe::Number(entry, "ownerLocation"));
            if (!Names(wanted, name, LocationName(owner))) continue;
            Found f{name, static_cast<std::uint32_t>(Safe::Number(entry, "space")),
                    {static_cast<float>(Safe::Number(entry, "x")), static_cast<float>(Safe::Number(entry, "y")), static_cast<float>(Safe::Number(entry, "z"))}};
            if (f.space == 0) continue;
            const int rank = f.space == npcSpace ? 0 : f.space == playerSpace ? 1 : 2;
            // "The kitchen" is the kitchen of the building they are in, not any kitchen someone named.
            if (roomWord && rank != 0) continue;
            const float distance = rank == 0 ? npcAt.GetDistance(f.at) : FLT_MAX / 2;
            if (rank < bestRank || (rank == bestRank && distance < bestDistance)) {
                best = f;
                bestRank = rank;
                bestDistance = distance;
            }
        }
        if (!best) return std::nullopt;

        Target target;
        target.name = best->name;
        auto* space = RE::TESForm::LookupByID(best->space);
        if (auto* cell = space ? space->As<RE::TESObjectCELL>() : nullptr) {
            if (player && player->GetParentCell() == cell) target.anchor = player;
            else if (npc->GetParentCell() == cell) target.anchor = npc;
            else target.anchor = DoorIn(cell);
        } else if (auto* world = space ? space->As<RE::TESWorldSpace>() : nullptr) {
            if (player && Outdoors(player) && player->GetWorldspace() == world) target.anchor = player;
            else if (Outdoors(npc) && npc->GetWorldspace() == world) target.anchor = npc;
            else target.anchor = MarkerNear(world, best->at);
        }
        if (!target.anchor) {
            logger::info("PlaceTargets: '{}' is a SkyrimNet place, but nothing in its cell or worldspace to go by", best->name);
            return std::nullopt;
        }
        target.offset = best->at - target.anchor->GetPosition();
        logger::info("PlaceTargets: '{}' for {} is the SkyrimNet place '{}' (by {:08X}, {:.0f} units off)", destination,
                     npc->GetDisplayFullName(), best->name, target.anchor->GetFormID(), target.offset.Length());
        return target;
    }
}
