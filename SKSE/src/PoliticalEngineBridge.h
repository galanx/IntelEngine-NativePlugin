#pragma once
#include "Plugin.h"
#include <atomic>
#include <cstdint>
#include <string>

namespace IntelEngine {
    class PoliticalEngineBridge final : public RE::BSTEventSink<SKSE::ModCallbackEvent> {
    public:
        static PoliticalEngineBridge* GetSingleton() {
            static PoliticalEngineBridge instance;
            return &instance;
        }
        void Initialize();
        void QueueCapture(std::string sections);
        bool IsActive() const { return active_.load(); }
        RE::BSEventNotifyControl ProcessEvent(const SKSE::ModCallbackEvent* event,
            RE::BSTEventSource<SKSE::ModCallbackEvent>*) override;
    private:
        PoliticalEngineBridge() = default;
        using Reader = std::uint32_t (*)(char*, std::uint32_t);
        Reader reader_ = nullptr;
        bool initialized_ = false;
        std::atomic<bool> active_{false};
        void Capture(const std::string& sections);
    };
}
