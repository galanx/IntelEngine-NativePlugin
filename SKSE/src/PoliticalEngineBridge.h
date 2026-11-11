#pragma once
#include "Plugin.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace IntelEngine {
    class PoliticalEngineBridge final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static PoliticalEngineBridge* GetSingleton() {
            static PoliticalEngineBridge instance;
            return &instance;
        }
        void Initialize();
        void MarkDirty(bool reset = false);
        void SetEnabled(bool on) { enabled_.store(on); MarkDirty(true); }
        nlohmann::json Poll();
        bool IsDetected() const { return reader_ != nullptr; }
        bool HasSidesExport() const { return sidesReader_ != nullptr; }
        nlohmann::json Context();
        bool IsActive() const { return active_.load(); }
        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>*) override;
    private:
        PoliticalEngineBridge() = default;
        using Reader = std::uint32_t (*)(char*, std::uint32_t);
        Reader initiativeReader_ = nullptr;
        Reader sidesReader_ = nullptr;
        nlohmann::json sides_;
        std::unordered_set<std::string> initiativeIds_;
        bool initiativeReady_ = false;
        Reader reader_ = nullptr;
        bool initialized_ = false;
        std::atomic<bool> active_{false};
        std::atomic<bool> enabled_{true};
        std::mutex mutex_;
        std::atomic<bool> dirty_{false};
        std::atomic<unsigned> resetEpoch_{0};
        unsigned baselineEpoch_ = 0;
        std::size_t revisionCount_ = 0;
        nlohmann::json baseline_, revision_;
    };
}
