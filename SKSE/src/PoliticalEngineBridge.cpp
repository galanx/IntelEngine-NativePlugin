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
        initiativeReader_ = reinterpret_cast<Reader>(GetProcAddress(dll, "PE_GetIntelInitiatives080JSON"));
        if (auto* source = SKSE::GetModCallbackEventSource()) source->AddEventSink(this);
        logger::info("PoliticalEngineBridge: detected API v3");
        MarkDirty(true);
        AsyncDispatch::Submit([this] { Poll(); });
    }

    void PoliticalEngineBridge::MarkDirty(bool reset) {
        if (reset) { resetEpoch_.fetch_add(1); active_.store(false); }
        dirty_.store(true);
    }
    nlohmann::json PoliticalEngineBridge::Context() {
        std::lock_guard lock(mutex_);
        if (!active_ || baselineEpoch_ != resetEpoch_.load()) return nullptr;
        return {{"territory", baseline_["regions"]}, {"realms", baseline_["realms"]}};
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
        auto journal = active_ && initiativeReader_ ? ReadJSON(initiativeReader_) : std::nullopt;
        if (!active_ || epoch != resetEpoch_.load()) { active_.store(false); baseline_ = nullptr; initiativeReady_ = false; return changes; }
        auto current = PEComparable(*snapshot);
        const auto& revision = current["revision"];
        if (!baseline_.is_object() || baselineEpoch_ != epoch) { revision_ = nullptr; revisionCount_ = 0; }
        const nlohmann::json missing;
        const auto& entries = journal ? PEField(PEField(*journal, "intel_initiatives080"), "events") : missing;
        auto initiatives = PEInitiatives(entries, initiativeIds_, !initiativeReady_ || !baseline_.is_object() || baselineEpoch_ != epoch);
        initiativeReady_ = entries.is_array();
        if (baseline_.is_object() && baselineEpoch_ == epoch && revision.is_number_unsigned()) {
            if (revision_ != revision) { revision_ = revision; revisionCount_ = 0; }
            changes = PEDiff(baseline_, current);
            changes.insert(changes.end(), initiatives.begin(), initiatives.end());
            for (auto& change : changes) change["realms"] = current["realms"];
            // ponytail: four public records per revision; raise only if real news is lost.
            if (changes.size() > 4 - revisionCount_) changes.erase(changes.begin() + (4 - revisionCount_), changes.end());
            revisionCount_ += changes.size();
        }
        baseline_ = std::move(current);
        baselineEpoch_ = epoch;
        return changes;
    }
}
