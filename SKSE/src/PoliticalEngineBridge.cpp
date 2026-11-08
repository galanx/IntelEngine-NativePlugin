#include "PoliticalEngineBridge.h"
#include "PoliticalEngineReadJSON.h"
#include "PoliticalEngineChanges.h"
#include "AsyncDispatch.h"
#include <Windows.h>
#include <algorithm>

namespace IntelEngine {
    void PoliticalEngineBridge::Initialize() {
        if (initialized_) return;
        initialized_ = true;
        auto dll = GetModuleHandleW(L"PoliticalLetters.dll");
        auto version = dll ? reinterpret_cast<std::uint32_t (*)()>(GetProcAddress(dll, "PE_GetApiVersion")) : nullptr;
        auto reader = dll ? reinterpret_cast<Reader>(GetProcAddress(dll, "PE_GetSnapshotJSON")) : nullptr;
        if (!version || !reader || version() != 3) {
            logger::info("PoliticalEngineBridge: unavailable; off for this session");
            return;
        }
        reader_ = reader;
        if (auto* source = SKSE::GetModCallbackEventSource()) source->AddEventSink(this);
        logger::info("PoliticalEngineBridge: detected API v3");
        MarkDirty(true);
        AsyncDispatch::Submit([this] { Poll(); });
    }

    void PoliticalEngineBridge::MarkDirty(bool reset) {
        if (reset) { resetEpoch_.fetch_add(1); active_.store(false); }
        dirty_.store(true);
    }

    RE::BSEventNotifyControl PoliticalEngineBridge::ProcessEvent(const SKSE::ModCallbackEvent* event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*) {
        if (event && event->eventName == "PL_StateChanged")
            MarkDirty(std::string_view(event->strArg.c_str()).find("\"loaded\"") != std::string_view::npos);
        return RE::BSEventNotifyControl::kContinue;
    }

    nlohmann::json PoliticalEngineBridge::Poll() {
        std::lock_guard lock(mutex_);
        auto changes = nlohmann::json::array();
        if (!reader_ || !dirty_.exchange(false)) return changes;
        const auto epoch = resetEpoch_.load();
        auto snapshot = ReadJSON(reader_);
        active_.store(snapshot && PEField(*snapshot, "data_ready") == true &&
            PEField(PEField(*snapshot, "status"), "bEnabled") == true);
        if (!active_ || epoch != resetEpoch_.load()) { active_.store(false); baseline_ = nullptr; return changes; }
        auto current = PEComparable(*snapshot);
        const auto& revision = current["revision"];
        if (!baseline_.is_object() || baselineEpoch_ != epoch) { revision_ = nullptr; revisionCount_ = 0; }
        if (baseline_.is_object() && baselineEpoch_ == epoch && revision.is_number_unsigned()) {
            if (revision_ != revision) { revision_ = revision; revisionCount_ = 0; }
            changes = PEDiff(baseline_, current);
            // ponytail: four public records per revision; raise only if real news is lost.
            if (changes.size() > 4 - revisionCount_) changes.erase(changes.begin() + (4 - revisionCount_), changes.end());
            revisionCount_ += changes.size();
        }
        baseline_ = std::move(current);
        baselineEpoch_ = epoch;
        return changes;
    }
}
