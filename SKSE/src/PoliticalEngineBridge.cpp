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
        sidesReader_ = reinterpret_cast<Reader>(GetProcAddress(dll, "PE_GetPoliticalSides080JSON"));
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
        if (!enabled_ || !active_ || baselineEpoch_ != resetEpoch_.load()) return nullptr;
        nlohmann::json context = {{"territory", baseline_["regions"]}, {"realms", baseline_["realms"]}};
        if (!playerRealm_.empty()) context["player_realm"] = playerRealm_;
        context["side_source"] =sidesReader_ ? "political_sides080" : "vanilla_civil_war_only";
        // PE 0.8.0 RC2 returns the sides fields at top level; accept a "political_sides080" wrapper too.
        const auto& table = sides_.contains("political_sides080") ? PEField(sides_, "political_sides080") : sides_;
        const auto& realms = PEField(table, "realms");
        if (sidesReader_ && PEField(table, "territory_rule") == "current_owner_realm" && realms.is_array()) {
            int lookup[10]; std::fill_n(lookup, 10, -1);
            for (const auto& realm : realms) {
                const auto& id = PEField(realm, "realm_id"); const auto& side = PEField(realm, "side_id");
                if (id.is_number_integer() && id >= 0 && id <= 9 && side.is_number_integer() && side >= 0 && side <= 3) lookup[id.get<unsigned>()] = side.get<int>();
            }
            for (auto& region : context["territory"]) {
                const auto& owner = region["owner"];
                if (owner.is_number_integer() && owner >= 0 && owner <= 9 && lookup[owner.get<unsigned>()] >= 0) region["side_id"] = lookup[owner.get<unsigned>()];
            }
        }
        return context;
    }

    nlohmann::json PoliticalEngineBridge::Rulers() {
        std::lock_guard lock(mutex_);
        if (!enabled_ || !active_ || baselineEpoch_ != resetEpoch_.load()) return nullptr;
        return PEField(baseline_, "rulers");
    }

    nlohmann::json PoliticalEngineBridge::RealmSides() {
        std::lock_guard lock(mutex_);
        if (!enabled_ || !active_ || baselineEpoch_ != resetEpoch_.load() || !sidesReader_) return nullptr;
        auto out = nlohmann::json::object();
        const auto& table = sides_.contains("political_sides080") ? PEField(sides_, "political_sides080") : sides_;
        const auto& realms = PEField(table, "realms");
        if (realms.is_array())
            for (const auto& realm : realms) {
                const auto& id = PEField(realm, "realm_id"); const auto& side = PEField(realm, "side_id");
                if (id.is_number_integer() && id >= 0 && id <= 9 && side.is_number_integer() && side >= 0 && side <= 3) out[std::to_string(id.get<int>())] = side.get<int>();
            }
        return out;
    }

    nlohmann::json PoliticalEngineBridge::OwnedRealms() {
        std::lock_guard lock(mutex_);
        if (!enabled_ || !active_ || baselineEpoch_ != resetEpoch_.load()) return nlohmann::json::object();
        return owned_;
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
        if (!enabled_) { active_.store(false); baseline_ = nullptr; initiativeReady_ = false; return changes; }
        if (!reader_ || !dirty_.exchange(false)) return changes;
        const auto epoch = resetEpoch_.load();
        auto snapshot = ReadJSON(reader_);
        active_.store(snapshot && PEField(*snapshot, "data_ready") == true &&
            PEField(PEField(*snapshot, "status"), "bEnabled") == true);
        auto journal = active_ && initiativeReader_ ? ReadJSON(initiativeReader_) : std::nullopt;
        auto sides = active_ && sidesReader_ ? ReadJSON(sidesReader_) : std::nullopt;
        if (!active_ || epoch != resetEpoch_.load()) {
            logger::debug("[diag:pe] inactive snapshot={} data_ready={} bEnabled={}", snapshot.has_value(),
                snapshot ? PEField(*snapshot, "data_ready").dump() : "-", snapshot ? PEField(PEField(*snapshot, "status"), "bEnabled").dump() : "-");
            active_.store(false); baseline_ = nullptr; initiativeReady_ = false; return changes;
        }
        auto current = PEComparable(*snapshot);
        sides_ = sides ? std::move(*sides) : nlohmann::json{};
        owned_ = PEOwnedRealms(*snapshot);
        playerRealm_.clear();
        if (const auto& rulers = PEField(*snapshot, "rulers"); rulers.is_array())
            for (const auto& r : rulers)
                if (PEField(r, "player_realm") == true && PEField(r, "realm").is_string()) playerRealm_ = PEField(r, "realm").get<std::string>();
        if (spdlog::default_logger()->should_log(spdlog::level::debug)) {
            std::string player = "none";
            if (const auto& rulers = PEField(*snapshot, "rulers"); rulers.is_array())
                for (const auto& r : rulers) if (PEField(r, "player_realm") == true) player = PEField(r, "realm").dump() + " id=" + PEField(r, "realm_id").dump();
            logger::debug("[diag:pe] poll revision={} playerRealm={} sidesReader={} sidesRaw={}", PEField(*snapshot, "revision").dump(), player,
                sidesReader_ != nullptr, sides_.dump().substr(0, 2000));
        }
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
            for (const auto& change : changes) logger::debug("[diag:pe] change {}", change.dump().substr(0, 600));
        }
        baseline_ = std::move(current);
        baselineEpoch_ = epoch;
        return changes;
    }
}
