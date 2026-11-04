#include "PoliticalEngineBridge.h"
#include "PoliticalEngineReadJSON.h"
#include "AsyncDispatch.h"
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <format>
#include <filesystem>
#include <fstream>

namespace IntelEngine {
    void PoliticalEngineBridge::Initialize() {
        if (initialized_) return;
        initialized_ = true;
        auto dll = GetModuleHandleW(L"PoliticalLetters.dll");
        auto version = dll ? reinterpret_cast<std::uint32_t (*)()>(GetProcAddress(dll, "PE_GetApiVersion")) : nullptr;
        auto reader = dll ? reinterpret_cast<Reader>(GetProcAddress(dll, "PE_GetSnapshotJSON")) : nullptr;
        if (!version || !reader || version() < 2) {
            logger::info("PoliticalEngineBridge: unavailable; off for this session");
            return;
        }
        reader_ = reader;
        if (auto* source = SKSE::GetModCallbackEventSource()) source->AddEventSink(this);
        logger::info("PoliticalEngineBridge: detected API >= 2");
        QueueCapture("[\"attached\"]");
    }

    // ponytail: temporary capture for design D2; remove when diff lands
    void PoliticalEngineBridge::QueueCapture(std::string sections) {
        if (!reader_) return;
        AsyncDispatch::Submit([this, sections = std::move(sections)] { Capture(sections); });
    }

    RE::BSEventNotifyControl PoliticalEngineBridge::ProcessEvent(const SKSE::ModCallbackEvent* event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*) {
        if (event && event->eventName == "PL_StateChanged") QueueCapture(event->strArg.c_str());
        return RE::BSEventNotifyControl::kContinue;
    }

    void PoliticalEngineBridge::Capture(const std::string& sections) {
        auto snapshot = ReadJSON(reader_);
        active_.store(false);
        if (!snapshot) return;
        if (snapshot->is_object()) {
            auto ready = snapshot->find("data_ready");
            auto status = snapshot->find("status");
            if (ready != snapshot->end() && *ready == true && status != snapshot->end() && status->is_object()) {
                auto enabled = status->find("bEnabled");
                active_.store(enabled != status->end() && *enabled == true);
            }
        }
        auto revision = snapshot->is_object() ? snapshot->find("revision") : snapshot->end();
        const auto revisionText = revision != snapshot->end() && revision->is_number_unsigned() ? revision->dump() :
            std::format("{:%Y%m%dT%H%M%S}Z", std::chrono::system_clock::now());
        const std::filesystem::path folder = "Data/SKSE/Plugins/IntelEngine/pe_snapshots";
        try {
            std::filesystem::create_directories(folder);
            const auto path = folder / ("pe_snapshot_" + revisionText + ".json");
            {
                std::ofstream file;
                file.exceptions(std::ios::failbit | std::ios::badbit);
                file.open(path, std::ios::trunc);
                file << snapshot->dump(2);
                file.close();
            }
            logger::info("PoliticalEngineBridge: wrote snapshot {}", path.string());
            std::ofstream events;
            events.exceptions(std::ios::failbit | std::ios::badbit);
            events.open(folder / "pe_events.log", std::ios::app);
            events << revisionText << ' ' << sections << '\n';
            events.close();
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
            for (const auto& entry : std::filesystem::directory_iterator(folder))
                if (entry.is_regular_file() && entry.path().filename().string().starts_with("pe_snapshot_") &&
                    entry.path().extension() == ".json") files.emplace_back(entry.last_write_time(), entry.path());
            std::sort(files.begin(), files.end());
            for (std::size_t i = 50; i < files.size(); ++i) std::filesystem::remove(files[i - 50].second);
        } catch (const std::exception& e) {
            logger::warn("PoliticalEngineBridge: snapshot capture failed: {}", e.what());
        }
    }
}
