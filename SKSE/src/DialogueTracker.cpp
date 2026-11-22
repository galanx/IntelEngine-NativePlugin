#include "DialogueTracker.h"
#include "BondBridge.h"
#include "SkyrimNetAPI.h"
#include "SpeechTracker.h"
#include <nlohmann/json.hpp>

namespace IntelEngine {

    // Helper: send a ModEvent on the game thread with the Actor as sender (avoids FormID encoding entirely)
    static void SendActorModEvent(RE::FormID formId, const char* eventName, const std::string& strArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        task->AddTask([formId, eventName = std::string(eventName), strArg]() {
            auto* actor = RE::TESForm::LookupByID<RE::Actor>(formId);
            if (!actor) return;
            auto* eventMgr = SKSE::GetModCallbackEventSource();
            if (!eventMgr) return;
            auto event = SKSE::ModCallbackEvent();
            event.eventName = eventName.c_str();
            event.strArg = strArg.c_str();
            event.numArg = 0.0f;
            event.sender = actor;
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

        // The player's own lines, for "has the player just said something" (an NPC's own cancel of a task counts
        // only then). Not tied to the auto-bio switch above: every line is noted.
        for (const char* type : SpeechTracker::kPlayerLineTypes) {
            const auto id = SkyrimNetAPI::RegisterEventCallback(type, [this](const char* json) {
                SpeechTracker::GetSingleton()->NotePlayerLine(SpeechTracker::Clock::now());
                Record(json, true);
            });
            if (id > 0) playerLineCallbackIds_.push_back(id);
        }
        logger::info("DialogueTracker: listening for the player's lines ({} of {} event types)",
                     playerLineCallbackIds_.size(), std::size(SpeechTracker::kPlayerLineTypes));
    }

    void DialogueTracker::Shutdown() {
        enabled_.store(false);
        if (callbackId_ > 0 && SkyrimNetAPI::UnregisterEventCallback) {
            SkyrimNetAPI::UnregisterEventCallback(callbackId_);
            callbackId_ = 0;
        }
        if (SkyrimNetAPI::UnregisterEventCallback) {
            for (const auto id : playerLineCallbackIds_) SkyrimNetAPI::UnregisterEventCallback(id);
        }
        playerLineCallbackIds_.clear();
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

    namespace {
        std::atomic<double> g_lastPlayerTalk{-1.0};   // steady-clock seconds of the last line to or by the player
    }

    float DialogueTracker::SecondsSincePlayerTalk() {
        const double last = g_lastPlayerTalk.load();
        if (last < 0.0) return -1.0f;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        return static_cast<float>(now - last);
    }

    void DialogueTracker::Record(const char* json, bool player) {
        if (!json) return;
        try {
            const auto j = nlohmann::json::parse(json, nullptr, false);
            if (!j.is_object()) return;
            Line line;
            line.at = std::chrono::steady_clock::now();
            line.speaker = player ? 0x14 : j.value("originatingActorFormId", static_cast<RE::FormID>(0));
            line.target = j.value("targetActorFormId", static_cast<RE::FormID>(0));
            const auto data = j.value("data", std::string());
            const auto parsed = nlohmann::json::parse(data, nullptr, false);
            line.text = parsed.is_object() && parsed.contains("dialogue") && parsed["dialogue"].is_string() ? parsed["dialogue"].get<std::string>() : data;
            if (line.text.size() > 600) line.text.resize(600);
            if (!line.speaker || line.text.empty()) return;
            if (player || line.target == 0x14) {
                g_lastPlayerTalk.store(std::chrono::duration<double>(line.at.time_since_epoch()).count());
            }
            // A line said to the player counts toward how well the two know each other (Bonds.h).
            if (!player && line.target == 0x14) BondBridge::GetSingleton()->Heard(line.speaker);
            std::lock_guard<std::mutex> lock(mutex_);
            lines_.push_back(std::move(line));
            while (lines_.size() > 80) lines_.pop_front();
        } catch (...) {
        }
    }

    std::vector<DialogueTracker::Line> DialogueTracker::ConversationWith(RE::FormID npc, float seconds, std::size_t max) const {
        std::vector<Line> out;
        const auto since = std::chrono::steady_clock::now() - std::chrono::milliseconds(static_cast<long long>(seconds * 1000.0f));
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& line : lines_) {
            if (line.at < since) continue;
            const bool theirs = line.speaker == npc;
            const bool players = line.speaker == 0x14 && (line.target == 0 || line.target == npc);
            if (theirs || players) out.push_back(line);
        }
        if (out.size() > max) out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(max));
        return out;
    }

    void DialogueTracker::OnDialogueEvent(const char* json) {
        Record(json, false);
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
                // Threshold reached — trigger bio update. Actor passed as sender (no FormID encoding needed)
                logger::info("DialogueTracker: threshold reached for FormID {:08X}", formId);
                SendActorModEvent(formId, "IntelEngine_AutoBioUpdate", "");
            } else if (newCount > 0 && newCount % 5 == 0) {
                // Persist intermediate count every 5 lines. strArg = count, sender = Actor
                SendActorModEvent(formId, "IntelEngine_SaveBioCount", std::to_string(newCount));
            }
        } catch (const std::exception& e) {
            logger::debug("DialogueTracker: Failed to parse dialogue event: {}", e.what());
        }
    }

}  // namespace IntelEngine
