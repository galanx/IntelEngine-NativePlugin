/**
 * Action Validator Implementation
 *
 * Returns machine-readable status codes only.
 * All narration/display text lives in Papyrus scripts.
 */

#include "ActionValidator.h"
#include "NPCIndex.h"
#include "LocationResolver.h"
#include "StringUtils.h"

namespace IntelEngine {

    bool ActionValidator::Validate(RE::Actor* actor, const std::string& actionType, const std::string& target) {
        std::string reason;
        std::string lowerAction = StringUtils::ToLowerStd(actionType);

        if (lowerAction == "travel") {
            return ValidateTravel(actor, target, reason);
        }
        if (lowerAction == "fetch_npc") {
            return ValidateFetchNPC(actor, target, reason);
        }
        if (lowerAction == "fetch_item") {
            return ValidateFetchItem(actor, target, reason);
        }
        if (lowerAction == "deliver_message") {
            return ValidateDeliverMessage(actor, target, reason);
        }

        return false;
    }

    RE::BSFixedString ActionValidator::GetFailureReason(RE::Actor* actor,
                                                         const std::string& actionType,
                                                         const std::string& target) {
        std::string reason;
        std::string lowerAction = StringUtils::ToLowerStd(actionType);

        if (lowerAction == "travel") {
            ValidateTravel(actor, target, reason);
        } else if (lowerAction == "fetch_npc") {
            ValidateFetchNPC(actor, target, reason);
        } else if (lowerAction == "fetch_item") {
            ValidateFetchItem(actor, target, reason);
        } else if (lowerAction == "deliver_message") {
            ValidateDeliverMessage(actor, target, reason);
        } else {
            reason = "unknown_action";
        }

        return RE::BSFixedString(reason);
    }

    bool ActionValidator::ValidateTravel(RE::Actor* actor, const std::string& destination, std::string& outReason) {
        if (destination.empty()) {
            outReason = "no_destination";
            return false;
        }

        auto* resolver = LocationResolver::GetSingleton();

        // Check if semantic term
        if (resolver->IsSemanticTerm(destination)) {
            if (!actor) {
                outReason = "no_actor";
                return false;
            }

            auto* marker = resolver->ResolveSemantic(actor, destination);
            if (!marker) {
                outReason = "no_path";
                return false;
            }

            return true;
        }

        // Named location
        auto* marker = resolver->Resolve(destination);
        if (!marker) {
            outReason = "location_not_found";
            return false;
        }

        return true;
    }

    bool ActionValidator::ValidateFetchNPC(RE::Actor* actor, const std::string& npcName, std::string& outReason) {
        if (npcName.empty()) {
            outReason = "npc_not_found";
            return false;
        }

        auto* index = NPCIndex::GetSingleton();
        auto* targetNPC = index->FindByName(npcName);

        if (!targetNPC) {
            outReason = "npc_not_found";
            return false;
        }

        if (targetNPC->IsDead()) {
            outReason = "npc_dead";
            return false;
        }

        if (!index->IsAccessible(targetNPC)) {
            outReason = "npc_not_accessible";
            return false;
        }

        if (actor && targetNPC == actor) {
            outReason = "npc_is_self";
            return false;
        }

        if (targetNPC == RE::PlayerCharacter::GetSingleton()) {
            outReason = "npc_is_player";
            return false;
        }

        return true;
    }

    bool ActionValidator::ValidateFetchItem(RE::Actor* actor, const std::string& itemName, std::string& outReason) {
        if (itemName.empty()) {
            outReason = "item_not_found";
            return false;
        }

        if (!actor) {
            outReason = "no_actor";
            return false;
        }

        auto inventory = actor->GetInventory();
        std::string lowerItem = StringUtils::ToLowerStd(itemName);

        bool found = false;
        for (const auto& [item, data] : inventory) {
            if (item) {
                std::string itemFullName = StringUtils::ToLowerStd(item->GetName());
                if (itemFullName.find(lowerItem) != std::string::npos) {
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            outReason = "item_not_found";
            return false;
        }

        return true;
    }

    bool ActionValidator::ValidateDeliverMessage(RE::Actor* actor, const std::string& recipient, std::string& outReason) {
        return ValidateFetchNPC(actor, recipient, outReason);
    }

}  // namespace IntelEngine
