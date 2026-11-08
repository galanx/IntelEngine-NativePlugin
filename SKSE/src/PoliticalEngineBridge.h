#pragma once
#include "Plugin.h"
#include <atomic>
#include <cstdint>
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

namespace IntelEngine {
    class PoliticalEngineBridge final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static PoliticalEngineBridge* GetSingleton() {
            static PoliticalEngineBridge instance;
            return &instance;
        }
        void Initialize();
        void MarkDirty(bool reset = false);
        nlohmann::json Poll();
        bool IsActive() const { return active_.load(); }
        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>*) override;
    private:
        PoliticalEngineBridge() = default;
        using Reader = std::uint32_t (*)(char*, std::uint32_t);
        Reader reader_ = nullptr;
        bool initialized_ = false;
        std::atomic<bool> active_{false};
        std::mutex mutex_;
        std::atomic<bool> dirty_{false};
        std::atomic<unsigned> resetEpoch_{0};
        unsigned baselineEpoch_ = 0;
        std::size_t revisionCount_ = 0;
        nlohmann::json baseline_, revision_;
    };
}
