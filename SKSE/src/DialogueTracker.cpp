#include "DialogueTracker.h"
#include "SkyrimNetAPI.h"
#include <nlohmann/json.hpp>

namespace IntelEngine {

    // Helper: send a ModEvent on the game thread with FormID as hex string (avoids float precision loss)
    static void SendFormIdModEvent(RE::FormID formId, const char* eventName, const std::string& extraArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string hexId = fmt::format("{:08X}", formId);
        task->AddTask([hexId, eventName = std::string(eventName), extraArg]() {
            auto* eventMgr = SKSE::GetModCallbackEventSource();
            if (!eventMgr) return;
            // strArg = "formIdHex|extraArg", numArg = 0
            std::string strArg = hexId + "|" + extraArg;
            auto event = SKSE::ModCallbackEvent();
            event.eventName = eventName.c_str();
            event.strArg = strArg.c_str();
            event.numArg = 0.0f;
            event.sender = nullptr;
            eventMgr->SendEvent(&event);
        });
    }

    void DialogueTracker::Initialize() {
        if (!SkyrimNetAPI::RegisterEventCallback) {
            logger::info("DialogueTracker: SkyrimNet event callback API not available — skipping");
            return;
        }

        callbackId_ = SkyrimNetAPI::RegisterEventCallback("dialogue",
            [this](const char* json) { OnDialogueEvent(json); });

        if (callbackId_ > 0) {
            logger::info("DialogueTracker: Registered dialogue callback (id={})", callbackId_);
        } else {
            logger::warn("DialogueTracker: Failed to register dialogue callback");
        }
    }

    void DialogueTracker::Shutdown() {
        enabled_.store(false);
        if (callbackId_ > 0 && SkyrimNetAPI::UnregisterEventCallback) {
            SkyrimNetAPI::UnregisterEventCallback(callbackId_);
            callbackId_ = 0;
        }
    }

    int DialogueTracker::GetCount(RE::FormID formId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lineCounts_.find(formId);
        return it != lineCounts_.end() ? it->second : 0;
    }

    void DialogueTracker::ResetCount(RE::FormID formId) {
        std::lock_guard<std::mutex> lock(mutex_);
        lineCounts_.erase(formId);
    }

    void DialogueTracker::SetCount(RE::FormID formId, int count) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count > 0) {
            lineCounts_[formId] = count;
        } else {
            lineCounts_.erase(formId);
        }
    }

    std::vector<std::pair<RE::FormID, int>> DialogueTracker::GetAllCounts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return {lineCounts_.begin(), lineCounts_.end()};
    }

    void DialogueTracker::OnDialogueEvent(const char* json) {
        if (!enabled_.load() || threshold_.load() <= 0) return;

        try {
            auto j = nlohmann::json::parse(json);

            RE::FormID formId = 0;
            if (j.contains("originatingActorFormId") && j["originatingActorFormId"].is_number()) {
                formId = j["originatingActorFormId"].get<RE::FormID>();
            } else if (j.contains("formId") && j["formId"].is_number()) {
                formId = j["formId"].get<RE::FormID>();
            }

            constexpr RE::FormID kPlayerFormID = 0x14;
            if (formId == 0 || formId == kPlayerFormID) return;

            int threshold = threshold_.load();
            bool shouldUpdate = false;
            int newCount = 0;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                lineCounts_[formId]++;
                newCount = lineCounts_[formId];
                if (newCount >= threshold) {
                    lineCounts_[formId] = 0;
                    newCount = 0;
                    shouldUpdate = true;
                }
            }

            if (shouldUpdate) {
                // Threshold reached — trigger bio update (also resets persisted count)
                logger::info("DialogueTracker: threshold reached for FormID {:08X}", formId);
                SendFormIdModEvent(formId, "IntelEngine_AutoBioUpdate", "");
            } else if (newCount > 0 && newCount % 5 == 0) {
                // Persist intermediate count every 5 lines (survives save/load)
                SendFormIdModEvent(formId, "IntelEngine_SaveBioCount", std::to_string(newCount));
            }
        } catch (const std::exception& e) {
            logger::debug("DialogueTracker: Failed to parse dialogue event: {}", e.what());
        }
    }

}  // namespace IntelEngine
