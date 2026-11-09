/**
 * Dashboard UI Manager Implementation
 *
 * UI host integration for the IntelEngine dashboard.
 * Hotkey polling and deferred host/state operations run on a background thread.
 *
 * Data flow:
 *   Papyrus -> PushFullState(json) -> IUiHost::Call("updateFullState", json) -> React
 *   React -> UI host listeners -> C++ -> SKSE ModEvent -> Papyrus
 */

#include "DashboardUIManager.h"
#include "DashboardConfig.h"
#include "FactionPolitics.h"
#include "PoliticalEngineBridge.h"
#include "SkyrimNetAPI.h"
#include "NPCIndex.h"
#include "SlotTracker.h"
#include "ProcessUtils.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <algorithm>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <stdexcept>
#include <string_view>
#include <cctype>

namespace IntelEngine {

    static std::string SafeDump(const nlohmann::json& value) {
        return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    static constexpr auto kPollIntervalMs = std::chrono::milliseconds(50);
    static constexpr auto kKeyPressCooldownMs = std::chrono::milliseconds(300);
    static constexpr const char* kActionYamlDirectory =
        "Data/SKSE/Plugins/SkyrimNet/external/galanx.intelengine/actions";

    static std::optional<std::uint32_t> ReadFormID(const nlohmann::json& json, const char* key) {
        const auto it = json.find(key);
        if (it == json.end()) return std::nullopt;
        if (it->is_number_unsigned()) {
            const auto value = it->get<std::uint64_t>();
            if (value != 0 && value <= std::numeric_limits<std::uint32_t>::max()) {
                return static_cast<std::uint32_t>(value);
            }
        } else if (it->is_number_integer()) {
            const auto value = it->get<std::int64_t>();
            if (value > 0 && static_cast<std::uint64_t>(value) <= std::numeric_limits<std::uint32_t>::max()) {
                return static_cast<std::uint32_t>(value);
            }
        }
        return std::nullopt;
    }


    void DashboardUIManager::Initialize() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initialized_) return;
            initialized_ = true;
            shuttingDown_ = false;
            acceptingCallbacks_.store(true);
            ++generation_;
            ++intentGeneration_;
        }
        static const UiListener listeners[] = {
            {"onCloseDashboard", OnCloseStatic},
            {"onRequestRefresh", OnRequestRefreshStatic},
            {"onReloadUI", OnReloadUIStatic},
            {"onDashboard_cancelTask", OnCancelTaskStatic},
            {"onDashboard_cancelSchedule", OnCancelScheduleStatic},
            {"onDashboard_cancelQuest", OnCancelQuestStatic},
            {"onDashboard_toggleStoryType", OnToggleStoryTypeStatic},
            {"onDashboard_changeSetting", OnChangeSettingStatic},
            {"onDashboard_removePackages", OnRemovePackagesStatic},
            {"onDashboard_changePluginConfig", OnChangePluginConfigStatic},
            {"onDashboard_dispatchStory", OnDispatchStoryStatic},
            {"onDashboard_dispatchNpcSocial", OnDispatchNpcSocialStatic},
            {"onDashboard_dispatchPolitics", OnDispatchPoliticsStatic},
            {"onDashboard_executeAction", OnExecuteActionStatic},
            {"onDashboard_toggleAction", OnToggleActionStatic},
            {"onDashboard_generateDirector", OnGenerateDirectorStatic},
        };

        std::unique_ptr<IUiHost> host;
        try {
            host = CreateUiHost(DashboardConfig::GetSingleton()->GetHostPreference(),
                                listeners,
                                [this](UiHostEvent event) { OnHostEvent(event); });
            if (!host || !host->IsAvailable() || !host->Create() || !host->IsAvailable()) {
                logger::info("[Dashboard] No dashboard UI host is available");
                if (host) host->Shutdown();
                return;
            }
        } catch (const std::exception& e) {
            logger::warn("[Dashboard] Failed to initialize UI host: {}", e.what());
            if (host) host->Shutdown();
            return;
        } catch (...) {
            logger::warn("[Dashboard] Failed to initialize UI host");
            if (host) host->Shutdown();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!shuttingDown_) {
                host_ = std::move(host);
                available_.store(true);
            }
        }
        if (host) {
            host->Shutdown();
            return;
        }

        stopPolling_.store(false);
        hotkeyThread_ = std::thread(&DashboardUIManager::HotkeyPollLoop, this);
        logger::info("[Dashboard] Hotkey polling thread started");
    }

    void DashboardUIManager::Shutdown() {
        bool logHostExit = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_ || shuttingDown_) return;
            logHostExit = requestedOpen_ || isOpen_.load();
            shuttingDown_ = true;
            acceptingCallbacks_.store(false);
            available_.store(false);
            ++generation_;
            ++intentGeneration_;
            pendingFullStateSet_ = false;
            pendingFullState_.clear();
            pendingSlotPayloadSet_ = false;
            pendingSlotPayload_.clear();
            pendingFullStatePayloadSet_ = false;
            pendingFullStatePayload_.clear();
        }
        if (logHostExit)
            logger::info("[Dashboard] Closed (cause=host exit; Esc/host reason unavailable)");
        stopPolling_.store(true);
        pollCondition_.notify_all();
        if (hotkeyThread_.joinable()) hotkeyThread_.join();
        std::unique_ptr<IUiHost> host;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            host = std::move(host_);
        }
        if (host) host->Shutdown();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requestedOpen_ = false;
            openedEventSent_ = false;
            domReady_.store(false);
            isOpen_.store(false);
            initialized_ = false;
        }
    }

    bool DashboardUIManager::IsAvailable() const {
        return available_.load();
    }

    void DashboardUIManager::Toggle() {
        bool logClose = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_ || !host_ || !available_.load()) return;
            const bool wasRequestedOpen = requestedOpen_;
            requestedOpen_ = !requestedOpen_;
            ++generation_;
            ++intentGeneration_;
            if (requestedOpen_) {
                if (!isOpen_.load()) openedEventSent_ = false;
                pendingOpen_ = true;
            } else {
                logClose = wasRequestedOpen;
                pendingClose_ = true;
                pendingCloseIntentGeneration_ = intentGeneration_;
                pendingOpen_ = false;
                isOpen_.store(false);
                openedEventSent_ = false;
                pendingFlush_ = false;
                pendingRefresh_ = false;
                pendingFullStateSet_ = false;
                pendingFullState_.clear();
                pendingSlotPayloadSet_ = false;
                pendingSlotPayload_.clear();
                pendingFullStatePayloadSet_ = false;
                pendingFullStatePayload_.clear();
            }
        }
        if (logClose) logger::info("[Dashboard] Closed (cause=hotkey)");
        pollCondition_.notify_one();
    }


    void DashboardUIManager::RequestClose() {
        bool logClose = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_ || !host_ || !available_.load()) return;
            logClose = requestedOpen_;
            requestedOpen_ = false;
            ++generation_;
            ++intentGeneration_;
            pendingClose_ = true;
            pendingCloseIntentGeneration_ = intentGeneration_;
            pendingOpen_ = false;
            isOpen_.store(false);
            openedEventSent_ = false;
            pendingFlush_ = false;
            pendingRefresh_ = false;
            pendingFullStateSet_ = false;
            pendingFullState_.clear();
            pendingSlotPayloadSet_ = false;
            pendingSlotPayload_.clear();
            pendingFullStatePayloadSet_ = false;
            pendingFullStatePayload_.clear();
        }
        if (logClose) logger::info("[Dashboard] Closed (cause=page close)");
        pollCondition_.notify_one();
    }

    void DashboardUIManager::OnHostEvent(UiHostEvent event, std::uint64_t expectedIntentGeneration) {
        bool notifyOpened = false;
        bool notifyRefresh = false;
        bool logOpened = false;
        bool logHostExit = false;
        bool logRefused = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (expectedIntentGeneration != 0 && intentGeneration_ != expectedIntentGeneration) return;
            if (shuttingDown_) return;
            switch (event) {
            case UiHostEvent::DomReady:
                domReady_.store(true);
                ++generation_;
                pendingFullStateSet_ = false;
                pendingFullState_.clear();
                pendingSlotPayloadSet_ = false;
                pendingSlotPayload_.clear();
                pendingFullStatePayloadSet_ = false;
                pendingFullStatePayload_.clear();
                knownNpcsSize_ = static_cast<std::size_t>(-1);
                pendingFlush_ = true;
                pendingRefresh_ = openedEventSent_;
                break;
            case UiHostEvent::Entered:
                if (!requestedOpen_) {
                    pendingClose_ = true;
                    pendingCloseIntentGeneration_ = intentGeneration_;
                    break;
                }
                expectedExitPending_ = false;
                logOpened = !isOpen_.load();
                isOpen_.store(true);
                pendingFlush_ = true;
                break;
            case UiHostEvent::Exited: {
                logHostExit = requestedOpen_;
                isOpen_.store(false);
                const bool expectedReopen = expectedExitPending_ &&
                    requestedOpen_ && intentGeneration_ > expectedExitIntentGeneration_;
                expectedExitPending_ = false;
                if (expectedReopen) {
                    logHostExit = false;
                    ++generation_;
                    pendingRefresh_ = false;
                    pendingFlush_ = false;
                    pendingFullStateSet_ = false;
                    pendingFullState_.clear();
                    pendingSlotPayloadSet_ = false;
                    pendingSlotPayload_.clear();
                    pendingFullStatePayloadSet_ = false;
                    pendingFullStatePayload_.clear();
                    break;
                }
                requestedOpen_ = false;
                openedEventSent_ = false;
                ++generation_;
                pendingRefresh_ = false;
                ++intentGeneration_;
                pendingOpen_ = false;
                pendingFlush_ = false;
                pendingFullStateSet_ = false;
                pendingFullState_.clear();
                pendingSlotPayloadSet_ = false;
                pendingSlotPayload_.clear();
                pendingFullStatePayloadSet_ = false;
                pendingFullStatePayload_.clear();
                break;
            }
            case UiHostEvent::Refused:
                logRefused = requestedOpen_;
                expectedExitPending_ = false;
                isOpen_.store(false);
                requestedOpen_ = false;
                openedEventSent_ = false;
                pendingRefresh_ = false;
                ++intentGeneration_;
                ++generation_;
                pendingOpen_ = false;
                pendingFlush_ = false;
                pendingFullStateSet_ = false;
                pendingFullState_.clear();
                pendingSlotPayloadSet_ = false;
                pendingSlotPayload_.clear();
                pendingFullStatePayloadSet_ = false;
                pendingFullStatePayload_.clear();
                break;
            case UiHostEvent::Unavailable:
                expectedExitPending_ = false;
                available_.store(false);
                isOpen_.store(false);
                domReady_.store(false);
                requestedOpen_ = false;
                openedEventSent_ = false;
                pendingRefresh_ = false;
                ++intentGeneration_;
                ++generation_;
                pendingOpen_ = false;
                pendingFlush_ = false;
                pendingFullStateSet_ = false;
                pendingFullState_.clear();
                pendingSlotPayloadSet_ = false;
                pendingSlotPayload_.clear();
                pendingFullStatePayloadSet_ = false;
                pendingFullStatePayload_.clear();
                break;
            }
            if (requestedOpen_ && domReady_.load() && isOpen_.load()) {
                if (!openedEventSent_) {
                    openedEventSent_ = true;
                    notifyOpened = true;
                } else if (pendingRefresh_) {
                    pendingRefresh_ = false;
                    notifyRefresh = true;
                }
            }
        }
        if (logOpened) logger::info("[Dashboard] Opened");
        if (logHostExit)
            logger::info("[Dashboard] Closed (cause=host exit; Esc/host reason unavailable)");
        if (logRefused) logger::info("[Dashboard] Open refused");
        pollCondition_.notify_one();
        if (notifyOpened) SendModEvent("IntelEngine_DashboardOpened");
        else if (notifyRefresh) SendModEvent("IntelEngine_DashboardRefresh");
    }


    void DashboardUIManager::FlushState(std::uint64_t generation) {
        bool ready;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready = !shuttingDown_ && generation_ == generation &&
                    requestedOpen_ && isOpen_.load() && domReady_.load();
        }
        if (ready) SendSlotData(generation);
    }

    void DashboardUIManager::PushSlotData() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_ || !available_.load() || !requestedOpen_) return;
            pendingFlush_ = true;
        }
        pollCondition_.notify_one();
    }

    void DashboardUIManager::SendSlotData(std::uint64_t generation) {
        if (!domReady_.load()) return;
        try {
            std::string json = SlotTracker::GetSingleton()->SerializeToJson();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (shuttingDown_ || generation_ != generation ||
                    !requestedOpen_ || !isOpen_.load() || !domReady_.load()) return;
                pendingSlotPayload_ = std::move(json);
                slotPayloadGeneration_ = generation;
                pendingSlotPayloadSet_ = true;
            }
            pollCondition_.notify_one();
        } catch (const std::exception& e) {
            logger::error("[Dashboard] Failed to serialize slot data: {}", e.what());
        }
    }

    void DashboardUIManager::PushFullState(const std::string& json) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_ || !available_.load() || !requestedOpen_) return;
            pendingFullState_ = json;
            pendingFullStateSet_ = true;
        }
        pollCondition_.notify_one();
    }

    // =========================================================================
    // Data Push (C++ -> JS)
    // =========================================================================


    void DashboardUIManager::ProcessFullState(const std::string& json, std::uint64_t generation) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_ || generation_ != generation ||
                !requestedOpen_ || !isOpen_.load() || !domReady_.load()) return;
        }
        try {
            // Fix Papyrus Bool-to-String before parsing — Papyrus produces
            // mixed-case TRUE/True/False/FALSE which is invalid JSON.
            // Do this FIRST so we always reach the plugin config injection.
            std::string fixed = json;
            auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
                size_t pos = 0;
                while ((pos = s.find(from, pos)) != std::string::npos) {
                    s.replace(pos, from.length(), to);
                    pos += to.length();
                }
            };
            replaceAll(fixed, "TRUE", "true");
            replaceAll(fixed, "FALSE", "false");
            replaceAll(fixed, "True", "true");
            replaceAll(fixed, "False", "false");

            auto parsed = nlohmann::json::parse(fixed);
            std::size_t knownNpcsSize = NPCIndex::GetSingleton()->GetNPCCount();
            if (auto* calendar = RE::Calendar::GetSingleton()) {
                parsed["calendar"] = {
                    {"time", calendar->GetCurrentGameTime()},
                    {"year", calendar->GetYear()}, {"month", calendar->GetMonth()},
                    {"day", calendar->GetDay()}, {"hour", calendar->GetHour()}
                };
            }

            // Inject plugin config values; politics comes from the live native settings state.
            nlohmann::json pc;
            if (SkyrimNetAPI::GetPluginConfigValue) {
                auto rv = [](const char* p, const char* d) -> std::string {
                    return SkyrimNetAPI::GetPluginConfigValue("IntelEngine", p, d);
                };
                try {
                    // Read hotkey/modifiers from DashboardConfig atomics (source of
                    // truth), NOT from SkyrimNet API which may have stale cache.
                    pc["ui.dashboard_hotkey"] = DashboardConfig::GetSingleton()->GetHotkey();
                    pc["ui.dashboard_modifiers"] = DashboardConfig::GetSingleton()->GetModifiers();
                    pc["ui.scale"] = std::stof(rv("ui.scale", "1.3"));
                    pc["story.faction_blocklist"] = rv("story.faction_blocklist", "");
                    pc["story.location_blocklist"] = rv("story.location_blocklist", "");
                    pc["story.npc_blocklist"] = rv("story.npc_blocklist", "");
                    pc["story.faction_whitelist"] = rv("story.faction_whitelist", "");
                    pc["story.location_whitelist"] = rv("story.location_whitelist", "");
                    pc["story.npc_whitelist"] = rv("story.npc_whitelist", "");
                    pc["llm.endpoint"] = rv("llm.endpoint", "");
                    pc["llm.api_key"] = rv("llm.api_key", "");
                    pc["llm.model_name"] = rv("llm.model_name", "");
                    pc["llm.temperature"] = std::stof(rv("llm.temperature", "0"));
                    pc["llm.max_tokens"] = std::stoi(rv("llm.max_tokens", "0"));
                    pc["llm.timeout"] = std::stoi(rv("llm.timeout", "0"));
                } catch (...) {}
            }
            auto* politics = FactionPolitics::GetSingleton();
            pc["politics.enabled"] = politics->IsEnabled();
            pc["politics.tick_interval_hours"] = politics->GetTickIntervalHours();
            pc["politics.max_relation_change_per_tick"] = politics->GetMaxRelationChangePerTick();
            pc["politics.max_active_wars"] = politics->GetMaxActiveWars();
            pc["politics.pe_reactions_move_relations"] = politics->GetPEReactionsMoveRelations();
            pc["politics.pe_detected"] = PoliticalEngineBridge::GetSingleton()->IsDetected();
            parsed["pluginConfig"] = pc;

            // Include nearby actors across exterior cell borders, but never across worldspaces.
            {
                nlohmann::json npcs = nlohmann::json::array();
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* playerCell = player ? player->GetParentCell() : nullptr;
                auto* playerWorld = player ? player->GetWorldspace() : nullptr;
                if (player && playerCell) {
                    const auto playerPos = player->GetPosition();
                    ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
                        if (!actor || actor == player) return false;
                        if (actor->IsDead() || actor->IsDisabled()) return false;
                        if (!actor->Is3DLoaded()) return false;
                        auto* actorCell = actor->GetParentCell();
                        if (!actorCell) return false;
                        const bool sameLocation = playerCell->IsInteriorCell()
                            ? actorCell == playerCell
                            : !actorCell->IsInteriorCell() && playerWorld &&
                                actor->GetWorldspace() == playerWorld;
                        if (!sameLocation) return false;
                        const auto actorPos = actor->GetPosition();
                        const float dx = actorPos.x - playerPos.x;
                        const float dy = actorPos.y - playerPos.y;
                        const float dz = actorPos.z - playerPos.z;
                        if (dx * dx + dy * dy + dz * dz > 4000.0f * 4000.0f) return false;
                        auto* base = actor->GetActorBase();
                        if (!base) return false;
                        nlohmann::json entry;
                        entry["formId"] = actor->GetFormID();
                        entry["name"] = actor->GetDisplayFullName();
                        npcs.push_back(entry);
                        return false;
                    });
                }
                parsed["loadedNpcs"] = npcs;
            }

            // Inject action metadata for Director/Actions tabs
            {
                std::lock_guard<std::mutex> cacheLock(actionCacheMutex_);
                if (!actionCacheLoaded_) LoadActionYAMLs();
                nlohmann::json acts = nlohmann::json::array();
                for (auto& a : actionCache_) {
                    nlohmann::json entry;
                    entry["name"] = a.name;
                    entry["fileName"] = a.fileName;
                    entry["description"] = a.description;
                    entry["category"] = a.category;
                    entry["isCategory"] = a.isCategory;
                    entry["enabled"] = a.enabled;
                    nlohmann::json params = nlohmann::json::array();
                    for (auto& p : a.params) {
                        nlohmann::json param = {{"name", p.name}, {"description", p.description}};
                        if (!p.enumValues.empty()) param["enum"] = p.enumValues;
                        params.push_back(std::move(param));
                    }
                    entry["params"] = params;
                    acts.push_back(entry);
                }
                parsed["actions"] = acts;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (shuttingDown_ || generation_ != generation ||
                    !requestedOpen_ || !isOpen_.load() || !domReady_.load()) return;
                // Coalesced snapshots retain names until the pending state can be delivered.
                if (knownNpcsSize_ != knownNpcsSize || pendingFullStatePayloadSet_) parsed["knownNpcs"] = NPCIndex::GetSingleton()->GetKnownNames();
                pendingFullStatePayload_ = SafeDump(parsed);
                fullStatePayloadGeneration_ = generation;
                knownNpcsSize_ = knownNpcsSize;
                pendingFullStatePayloadSet_ = true;
            }
            pollCondition_.notify_one();
        } catch (const std::exception& e) {
            logger::error("[Dashboard] Failed to push full state: {}", e.what());
        }
    }

    // =========================================================================
    // Hotkey Polling
    // =========================================================================

    void DashboardUIManager::HotkeyPollLoop() {
        bool wasComboPressed = false;
        auto lastToggle = std::chrono::steady_clock::now();
        while (!stopPolling_.load()) {

            std::unique_lock<std::mutex> lock(mutex_);
            pollCondition_.wait_for(lock, kPollIntervalMs, [this] {
                const bool ready = requestedOpen_ && isOpen_.load() && domReady_.load() && !shuttingDown_;
                return stopPolling_.load() || pendingOpen_ || pendingClose_ ||
                       pendingReload_ || !pendingDirectorResults_.empty() || (ready && !gameTaskQueued_ &&
                           (pendingFlush_ || pendingFullStateSet_ ||
                            pendingSlotPayloadSet_ || pendingFullStatePayloadSet_));
            });
            if (stopPolling_.load()) break;
            if (gameTaskRetry_) {
                gameTaskQueued_ = false;
                gameTaskRetry_ = false;
            }
            bool open = std::exchange(pendingOpen_, false);
            const std::uint64_t closeIntentGeneration = pendingCloseIntentGeneration_;
            bool close = std::exchange(pendingClose_, false);
            bool reload = std::exchange(pendingReload_, false);
            const bool canProcessState =
                !reload && !gameTaskQueued_ && requestedOpen_ && isOpen_.load() &&
                domReady_.load() && !shuttingDown_;
            const std::uint64_t generation = generation_;
            const std::uint64_t intentGeneration = intentGeneration_;
            bool sendSlots = canProcessState && pendingSlotPayloadSet_ &&
                             slotPayloadGeneration_ == generation;
            bool sendFullState = canProcessState && pendingFullStatePayloadSet_ &&
                                 fullStatePayloadGeneration_ == generation;
            std::string slotsPayload;
            std::string fullStatePayload;
            if (sendSlots) {
                slotsPayload = std::move(pendingSlotPayload_);
                pendingSlotPayloadSet_ = false;
            } else if (pendingSlotPayloadSet_ && slotPayloadGeneration_ != generation) {
                pendingSlotPayloadSet_ = false;
                pendingSlotPayload_.clear();
            }
            if (sendFullState) {
                fullStatePayload = std::move(pendingFullStatePayload_);
                pendingFullStatePayloadSet_ = false;
            } else if (pendingFullStatePayloadSet_ && fullStatePayloadGeneration_ != generation) {
                pendingFullStatePayloadSet_ = false;
                pendingFullStatePayload_.clear();
            }
            bool flush = canProcessState && std::exchange(pendingFlush_, false);
            bool fullState = canProcessState && std::exchange(pendingFullStateSet_, false);
            std::string state;
            if (fullState) state = std::move(pendingFullState_);
            auto directorResults = std::move(pendingDirectorResults_);
            pendingDirectorResults_.clear();
            const bool queueGameTask = flush || fullState;
            if (queueGameTask) gameTaskQueued_ = true;
            lock.unlock();
            if (close) {
                bool callClose;
                {
                    std::lock_guard<std::mutex> commandLock(mutex_);
                    callClose = !shuttingDown_;
                    if (callClose) {
                        expectedExitPending_ = true;
                        expectedExitIntentGeneration_ = closeIntentGeneration;
                    }
                }
                if (callClose && host_) host_->Close();
            }
            if (open) {
                bool stillRequested;
                {
                    std::lock_guard<std::mutex> commandLock(mutex_);
                    stillRequested = requestedOpen_ && !shuttingDown_ &&
                                     intentGeneration_ == intentGeneration;
                }
                if (stillRequested) {
                    const auto result = host_ ? host_->Open() : UiOpenResult::Refused;
                    if (result == UiOpenResult::Refused)
                        OnHostEvent(UiHostEvent::Refused, intentGeneration);
                }
            }
            if (reload) {
                DashboardConfig::GetSingleton()->Reload();
                {
                    std::lock_guard<std::mutex> cacheLock(actionCacheMutex_);
                    actionCacheLoaded_ = false;
                }
                if (host_) host_->Reload();
                {
                    std::lock_guard<std::mutex> reloadLock(mutex_);
                    domReady_.store(false);
                }
            }
            std::uint64_t serviceGeneration;
            {
                std::lock_guard<std::mutex> serviceLock(mutex_);
                serviceGeneration = intentGeneration_;
            }
            if (host_ && host_->Service() == UiOpenResult::Refused)
                OnHostEvent(UiHostEvent::Refused, serviceGeneration);
            if (sendSlots) {
                bool current;
                {
                    std::lock_guard<std::mutex> stateLock(mutex_);
                    current = !shuttingDown_ && generation_ == generation &&
                              requestedOpen_ && isOpen_.load() && domReady_.load();
                }
                if (current && host_) host_->Call("updateSlots", slotsPayload);
            }
            if (sendFullState) {
                bool current;
                {
                    std::lock_guard<std::mutex> stateLock(mutex_);
                    current = !shuttingDown_ && generation_ == generation &&
                              requestedOpen_ && isOpen_.load() && domReady_.load();
                }
                if (current && host_) host_->Call("updateFullState", fullStatePayload);
            }
            for (const auto& [resultGeneration, payload] : directorResults) {
                bool current;
                {
                    std::lock_guard<std::mutex> resultLock(mutex_);
                    current = !shuttingDown_ && generation_ == resultGeneration && requestedOpen_ &&
                              isOpen_.load() && domReady_.load();
                }
                if (current && host_) host_->Call("directorGenerateResult", payload);
            }
            if (queueGameTask) {
                auto* task = SKSE::GetTaskInterface();
                if (task) {
                    task->AddTask([this, generation, flush, fullState, state = std::move(state)]() mutable {
                        bool current;
                        {
                            std::lock_guard<std::mutex> taskLock(mutex_);
                            current = !shuttingDown_ && generation_ == generation &&
                                      requestedOpen_ && isOpen_.load() && domReady_.load();
                        }
                        if (current) {
                            if (flush) FlushState(generation);
                            if (fullState) ProcessFullState(state, generation);
                        }
                        {
                            std::lock_guard<std::mutex> taskLock(mutex_);
                            gameTaskQueued_ = false;
                        }
                        pollCondition_.notify_one();
                    });
                } else {
                    std::lock_guard<std::mutex> retryLock(mutex_);
                    if (!shuttingDown_ && generation_ == generation) {
                        pendingFlush_ |= flush;
                        if (fullState) {
                            pendingFullState_ = std::move(state);
                            pendingFullStateSet_ = true;
                        }
                    }
                    gameTaskRetry_ = true;
                }
            }

            HWND foregroundWindow = GetForegroundWindow();
            DWORD foregroundProcessId = 0;
            if (!foregroundWindow ||
                GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId) == 0 ||
                foregroundProcessId != GetCurrentProcessId()) {
                wasComboPressed = true;
                continue;
            }

            auto* ui = RE::UI::GetSingleton();
            if (ui && ui->GameIsPaused() && !isOpen_.load()) continue;

            auto* config = DashboardConfig::GetSingleton();
            int vk = config->GetHotkey();
            int mods = config->GetModifiers();
            if (vk < 0 || vk > 255) continue;
            
            const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            const bool modifiersOk =
                ctrlDown == ((mods & kModCtrl) != 0) &&
                shiftDown == ((mods & kModShift) != 0) &&
                altDown == ((mods & kModAlt) != 0);
            const bool comboPressed = modifiersOk && (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (comboPressed && !wasComboPressed) {
                const auto now = std::chrono::steady_clock::now();
                if (now - lastToggle > kKeyPressCooldownMs) {
                    lastToggle = now;
                    auto* task = SKSE::GetTaskInterface();
                    if (task) {
                        task->AddTask([]() {
                            DashboardUIManager::GetSingleton()->Toggle();
                        });
                    }
                }
            }
            wasComboPressed = comboPressed;
        }
    }

    // =========================================================================
    // Static callbacks (UI host -> manager)
    // =========================================================================

    void DashboardUIManager::OnCloseStatic(const char*) {
        GetSingleton()->RequestClose();
    }

    void DashboardUIManager::OnRequestRefreshStatic(const char*) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        auto* manager = GetSingleton();
        manager->SendModEvent("IntelEngine_DashboardRefresh");
        manager->PushSlotData();
    }

    void DashboardUIManager::OnReloadUIStatic(const char*) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        GetSingleton()->ReloadView();
    }

    // Dashboard action callbacks from JS
    void DashboardUIManager::OnCancelTaskStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            int slot = j.value("slot", -1);
            if (slot >= 0) {
                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardCancelTask", "", static_cast<float>(slot));
            }
        } catch (...) {}
    }

    void DashboardUIManager::OnCancelQuestStatic(const char* /*jsonArg*/) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        GetSingleton()->SendModEvent("IntelEngine_DashboardCancelQuest", "", 0.0f);
    }

    void DashboardUIManager::OnCancelScheduleStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            int slot = j.value("slot", -1);
            if (slot >= 0) {
                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardCancelSchedule", "", static_cast<float>(slot));
            }
        } catch (...) {}
    }

    void DashboardUIManager::OnToggleStoryTypeStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            std::string type = j.value("type", "");
            bool enabled = j.value("enabled", true);
            if (!type.empty()) {
                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardToggleStory", type, enabled ? 1.0f : 0.0f);
            }
        } catch (...) {}
    }

    void DashboardUIManager::OnChangeSettingStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            std::string key = j.value("key", "");
            if (!key.empty()) {
                // Pack the value as numArg for numeric settings, strArg for key
                float value = 0.0f;
                if (j["value"].is_boolean()) value = j["value"].get<bool>() ? 1.0f : 0.0f;
                else if (j["value"].is_number()) value = j["value"].get<float>();
                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardSetting", key, value);
            }
        } catch (...) {}
    }

    void DashboardUIManager::OnRemovePackagesStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            const auto formId = ReadFormID(j, "formId");
            if (formId) {
                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardRemovePackages", std::to_string(*formId));
            }
        } catch (...) {}
    }

    void DashboardUIManager::OnChangePluginConfigStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            std::string path = j.value("path", "");
            if (path.empty()) return;
        
            // Split "section.key" into section and key
            auto dot = path.find('.');
            if (dot == std::string::npos) return;
            std::string section = path.substr(0, dot);
            std::string key = path.substr(dot + 1);
        
            // Format value for YAML based on JSON type
            std::string yamlValue;
            auto& val = j["value"];
            if (val.is_string()) {
                std::string s = val.get<std::string>();
                // Escape backslashes and quotes for YAML double-quoted string
                std::string escaped;
                escaped.reserve(s.size() + 2);
                for (char c : s) {
                    if (c == '\\') escaped += "\\\\";
                    else if (c == '"') escaped += "\\\"";
                    else escaped += c;
                }
                yamlValue = "\"" + escaped + "\"";
            } else if (val.is_number_integer()) {
                yamlValue = std::to_string(val.get<int>());
            } else if (val.is_number_float()) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2f", val.get<double>());
                std::string s(buf);
                // Trim trailing zeros (1.70 -> 1.7, 1.00 -> 1)
                if (s.find('.') != std::string::npos) {
                    while (s.back() == '0') s.pop_back();
                    if (s.back() == '.') s.pop_back();
                }
                yamlValue = s;
            } else if (val.is_boolean()) {
                yamlValue = val.get<bool>() ? "true" : "false";
            } else {
                yamlValue = SafeDump(val);
            }
        
            auto* config = DashboardConfig::GetSingleton();
        
            // Hotkey/modifier: use dedicated setters that write YAML + update
            // atomics directly.  Do NOT call Reload() — the SkyrimNet API
            // cache is stale after a direct file write and would revert the
            // change to the old value.
            if (key == "dashboard_hotkey" && val.is_number_integer()) {
                int vk = val.get<int>();
                if (config->SetHotkey(vk)) {
                    logger::info("[Dashboard] Hotkey updated: VK {}", vk);
                } else {
                    logger::warn("[Dashboard] Failed to save hotkey VK {}", vk);
                }
                DashboardUIManager::GetSingleton()->SendModEvent("IntelEngine_DashboardRefresh");
                return;
            }
            if (key == "dashboard_modifiers" && val.is_number_integer()) {
                int mods = val.get<int>();
                if (config->SetModifiers(mods)) {
                    logger::info("[Dashboard] Modifiers updated: {}", mods);
                } else {
                    logger::warn("[Dashboard] Failed to save modifiers {}", mods);
                }
                DashboardUIManager::GetSingleton()->SendModEvent("IntelEngine_DashboardRefresh");
                return;
            }
        
            if (section == "politics") {
                auto* politics = FactionPolitics::GetSingleton();
                bool saved = false;
                if (key == "enabled" && val.is_boolean()) {
                    saved = politics->SetEnabled(val.get<bool>());
                } else if (key == "tick_interval_hours" && val.is_number_integer()) {
                    saved = politics->SetTickIntervalHours(val.get<int>());
                } else if (key == "max_relation_change_per_tick" && val.is_number_integer()) {
                    saved = politics->SetMaxRelationChangePerTick(val.get<int>());
                } else if (key == "max_active_wars" && val.is_number_integer()) {
                    saved = politics->SetMaxActiveWars(val.get<int>());
                } else if (key == "pe_reactions_move_relations" && val.is_boolean()) {
                    saved = politics->SetPEReactionsMoveRelations(val.get<bool>());
                } else {
                    logger::warn("[Dashboard] Invalid politics setting value: {}", path);
                }
                if (saved) DashboardUIManager::GetSingleton()->SendModEvent("IntelEngine_DashboardRefresh");
                return;
            }
        
            if (config->WriteYamlValue(section, key, yamlValue)) {
                logger::info("[Dashboard] Plugin config updated: {}.{} = {}", section, key, yamlValue);
                DashboardUIManager::GetSingleton()->SendModEvent("IntelEngine_DashboardRefresh");
            } else {
                logger::warn("[Dashboard] Failed to write plugin config: {}.{}", section, key);
            }
        } catch (...) {}
    }

    // =========================================================================
    // ModEvent (C++ -> Papyrus)
    // =========================================================================

    void DashboardUIManager::SendModEvent(const std::string& eventName,
                                           const std::string& strArg, float numArg) {
        auto* eventSource = SKSE::GetModCallbackEventSource();
        if (!eventSource) {
            logger::warn("[Dashboard] ModCallbackEventSource not available");
            return;
        }

        SKSE::ModCallbackEvent modEvent{};
        modEvent.eventName = eventName;
        modEvent.strArg = strArg;
        modEvent.numArg = numArg;
        modEvent.sender = nullptr;
        eventSource->SendEvent(&modEvent);

        logger::debug("[Dashboard] Sent ModEvent: {} str={} num={}",
                    eventName, strArg, numArg);
    }

    void DashboardUIManager::OnGenerateDirectorStatic(const char* jsonArg) {
        auto* mgr = GetSingleton();
        if (!mgr->acceptingCallbacks_.load()) return;
        try {
            auto request = nlohmann::json::parse(jsonArg ? jsonArg : "{}");
            const auto id = request.at("requestId").get<std::string>();
            const auto form = request.at("form").get<std::string>();
            if (id.empty() || (form != "story" && form != "social" && form != "political" && form != "action")) return;
            if (request.contains("outcome")) { logger::info("[Dashboard] Generate form={} request={} outcome={}", form, id, SafeDump(request["outcome"])); return; }
            std::uint64_t generation;
            { std::lock_guard<std::mutex> lock(mgr->mutex_);
              if (mgr->shuttingDown_ || !mgr->requestedOpen_ || !mgr->isOpen_ || !mgr->domReady_) return;
              generation = mgr->generation_; }
            auto queue = [mgr, generation, id, form](nlohmann::json fields, std::string error, nlohmann::json rejected = nlohmann::json::object()) {
                nlohmann::json result{{"requestId", id}, {"form", form}, {"ok", error.empty()},
                                      {"fields", std::move(fields)}, {"error", std::move(error)}, {"rejected", std::move(rejected)}};
                { std::lock_guard<std::mutex> lock(mgr->mutex_);
                  if (mgr->generation_ != generation || mgr->shuttingDown_) return;
                  mgr->pendingDirectorResults_.emplace_back(generation, SafeDump(result)); }
                mgr->pollCondition_.notify_one();
            };
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                auto* cell = player->GetParentCell();
                const char* location = cell && cell->IsInteriorCell() ? cell->GetName() : nullptr;
                if (!location || !location[0]) {
                    auto* current = player->GetCurrentLocation();
                    location = current ? current->GetFullName() : nullptr;
                }
                request["currentLocation"] = location && location[0] ? location : "";
            } else request["currentLocation"] = "";
            if (!SkyrimNetAPI::SendCustomPromptToLLM) { queue({}, "Generate unavailable"); return; }
            const auto context = SafeDump(request);
            const bool queued = SkyrimNetAPI::SendCustomPromptToLLM("intel_director_generate", "", context.c_str(),
                [queue, form, id](const char* response, int success) mutable {
                    std::string_view raw = response ? response : "";
                    logger::info("[Dashboard] Generate form={} request={} raw={}", form, id, raw.substr(0, 600));
                    if (!success) { queue({}, "Generation failed"); return; }
                    try {
                        const auto first = raw.find('{'), last = raw.rfind('}');
                        auto fields = nlohmann::json::parse(first != raw.npos && last != raw.npos ? raw.substr(first, last - first + 1) : raw);
                        if (!fields.is_object() || fields.empty()) throw std::runtime_error("invalid fields");
                        nlohmann::json rejected = nlohmann::json::object();
                        for (const char* key : {"gossip", "narration"}) if (fields.contains(key) && fields[key].is_string()) {
                            const auto& text = fields[key].get_ref<const std::string&>();
                            auto has = [&](std::string_view phrase) { return std::search(text.begin(), text.end(), phrase.begin(), phrase.end(),
                                [](unsigned char a, unsigned char b) { return std::tolower(a) == b; }) != text.end(); };
                            if (has("subject npc") || has("another local npc")) { fields.erase(key); rejected[key] = "Placeholder NPC reference"; }
                        }
                        queue(std::move(fields), "", std::move(rejected));
                    } catch (...) { queue({}, "Malformed generation result: " + std::string(raw.substr(0, 120))); }
                });
            if (!queued) queue({}, "Generation unavailable");
        } catch (...) {}
    }

    // =========================================================================
    // Director: Story Dispatch (JS -> C++ -> pending params -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnDispatchStoryStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            std::string npcName = j.value("npcName", "");
            std::string type = j.value("storyType", "");
            std::string narration = j.value("narration", "");
            std::string replaceGeneration = j.value("replaceGeneration", "-1");
            if (npcName.empty() || type.empty() || narration.empty()) return;
        
            // Build response JSON with proper escaping (all optional fields default to "")
            nlohmann::json response;
            for (const char* f : {"sender","subject","gossip","destination",
                                  "msgContent","meetTime","questLocation",
                                  "enemyType","questSubType","victimName",
                                  "itemName","itemDesc"}) {
                response[f] = "";
            }
            for (auto& [key, val] : j.items()) {
                if (key == "npcName" || key == "storyType" || key == "narration") continue;
                if (val.is_string()) {
                    response[key] = val.get<std::string>();
                }
            }
        
            // Build event payload directly from locals — no shared state needed
            nlohmann::json eventPayload;
            eventPayload["storyType"] = type;
            eventPayload["narration"] = narration;
            eventPayload["npcName"] = npcName;
            eventPayload["response"] = SafeDump(response);
            eventPayload["replaceGeneration"] = replaceGeneration;
        
            DashboardUIManager::GetSingleton()->SendModEvent(
                "IntelEngine_DashboardDispatchStory", SafeDump(eventPayload), 0.0f);
            logger::info("[Dashboard] Director: dispatch story type={} npc={}", type, npcName);
        } catch (const std::exception& e) {
            logger::error("[Dashboard] Director story dispatch failed: {}", e.what());
        } catch (...) {
            logger::error("[Dashboard] Director story dispatch failed: unknown error");
        }
    }

    // =========================================================================
    // Director: NPC Social Dispatch (JS -> C++ -> pending params -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnDispatchNpcSocialStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            std::string npc1 = j.value("npc1Name", "");
            std::string npc2 = j.value("npc2Name", "");
            std::string type = j.value("socialType", "");
            std::string narration = j.value("narration", "");
            if (npc1.empty() || npc2.empty() || type.empty() || narration.empty()) return;
            std::string replaceGeneration = j.value("replaceGeneration", "-1");
        
            // Build response JSON matching what the NPC DM would return
            nlohmann::json response;
            response["should_act"] = true;
            response["type"] = type;
            response["npc"] = npc1;
            response["npc2"] = npc2;
            response["narration"] = narration;
            for (const char* f : {"fact1", "fact2", "gossip"}) {
                response[f] = "";
            }
            for (auto& [key, val] : j.items()) {
                if (key == "npc1Name" || key == "npc2Name" || key == "socialType" || key == "narration") continue;
                if (val.is_string()) response[key] = val.get<std::string>();
            }
        
            // Build event payload directly from locals — no shared state needed
            nlohmann::json eventPayload;
            eventPayload["socialType"] = type;
            eventPayload["narration"] = narration;
            eventPayload["npc1Name"] = npc1;
            eventPayload["npc2Name"] = npc2;
            eventPayload["response"] = SafeDump(response);
            eventPayload["replaceGeneration"] = replaceGeneration;
        
            DashboardUIManager::GetSingleton()->SendModEvent(
                "IntelEngine_DashboardDispatchNpcSocial", SafeDump(eventPayload), 0.0f);
            logger::info("[Dashboard] Director: NPC social type={} npc1={} npc2={}", type, npc1, npc2);
        } catch (const std::exception& e) {
            logger::error("[Dashboard] Director NPC social dispatch failed: {}", e.what());
        } catch (...) {
            logger::error("[Dashboard] Director NPC social dispatch failed: unknown error");
        }
    }

    // =========================================================================
    // Director: Political Event Dispatch (JS -> C++ -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnDispatchPoliticsStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            std::string factionA = j.value("factionA", "");
            std::string factionB = j.value("factionB", "");
            std::string eventType = j.value("eventType", "");
            std::string eventLabel = j.value("eventLabel", eventType);
            std::string description = j.value("description", "");
            int delta = j.value("relationDelta", 0);
            if (factionA.empty() || factionB.empty() || eventType.empty() || description.empty() || delta == 0) return;
        
            // Build a fake Political DM response matching the expected JSON format
            nlohmann::json response;
            response["should_act"] = true;
            response["faction_a"] = factionA;
            response["faction_b"] = factionB;
            response["faction_a_label"] = j.value("factionALabel", factionA); response["faction_b_label"] = j.value("factionBLabel", factionB);
            response["event_type"] = eventType;
            response["event_label"] = eventLabel;
            response["description"] = description;
            response["relation_delta"] = delta;
        
            // Pass the full response JSON as strArg — Papyrus feeds it to ProcessPoliticalDMResponse
            DashboardUIManager::GetSingleton()->SendModEvent(
                "IntelEngine_DashboardDispatchPolitics", SafeDump(response), 0.0f);
            logger::info("[Dashboard] Director: politics event={} factionA={} factionB={} delta={}",
                eventType, factionA, factionB, delta);
        } catch (const std::exception& e) {
            logger::error("[Dashboard] Director politics dispatch failed: {}", e.what());
        } catch (...) {
            logger::error("[Dashboard] Director politics dispatch failed: unknown error");
        }
    }

    // =========================================================================
    // Director: Action Execution (JS -> C++ -> pending params -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnExecuteActionStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            const auto formId = ReadFormID(j, "npcFormId");
            std::string actionName = j.value("actionName", "");
            if (!formId || actionName.empty()) return;
        
            // Pass action params directly in the ModEvent payload.
            nlohmann::json eventPayload;
            eventPayload["actionName"] = actionName;
            if (j.contains("params") && j["params"].is_object()) {
                for (auto& [key, val] : j["params"].items()) {
                    eventPayload[key] = val.is_string() ? val.get<std::string>() : SafeDump(val);
                }
            }
            eventPayload["npcFormId"] = std::to_string(*formId);
            DashboardUIManager::GetSingleton()->SendModEvent(
                "IntelEngine_DashboardExecuteAction", SafeDump(eventPayload));
            logger::info("[Dashboard] Director: execute action={} npc=0x{:X}", actionName, *formId);
        } catch (...) {}
    }

    // =========================================================================
    // Actions Tab: Toggle enabled in YAML (persistent, restart required)
    // =========================================================================

    void DashboardUIManager::OnToggleActionStatic(const char* jsonArg) {
        if (!GetSingleton()->acceptingCallbacks_.load()) return;
        
        
        std::string arg(jsonArg ? jsonArg : "{}");
        try {
            auto j = nlohmann::json::parse(arg);
            std::string name = j.value("name", "");
            bool enabled = j.value("enabled", true);
            if (name.empty()) return;
        
            auto* mgr = DashboardUIManager::GetSingleton();
        
            // Find action in cache to get its fileName
            std::string fileName;
            {
                std::lock_guard<std::mutex> cacheLock(mgr->actionCacheMutex_);
                for (auto& a : mgr->actionCache_) {
                    if (a.name == name) {
                        fileName = a.fileName;
                        a.enabled = enabled;  // Update cache immediately
                        break;
                    }
                }
            }
            if (fileName.empty()) return;
        
            // Modify the YAML file on disk
            std::string yamlPath = std::string(kActionYamlDirectory) + "/" + fileName + ".yaml";
            std::ifstream inFile(yamlPath);
            if (!inFile.is_open()) {
                logger::warn("[Dashboard] Cannot open action YAML: {}", yamlPath);
                return;
            }
        
            // Line-based replacement: only match top-level "enabled:" key
            std::vector<std::string> lines;
            std::string line;
            bool replaced = false;
            while (std::getline(inFile, line)) {
                if (!replaced && line.rfind("enabled:", 0) == 0) {
                    lines.push_back(std::string("enabled: ") + (enabled ? "true" : "false"));
                    replaced = true;
                } else {
                    lines.push_back(line);
                }
            }
            inFile.close();
        
            if (replaced) {
                std::ofstream outFile(yamlPath);
                if (outFile.is_open()) {
                    for (size_t i = 0; i < lines.size(); ++i) {
                        outFile << lines[i];
                        if (i + 1 < lines.size()) outFile << '\n';
                    }
                    outFile.close();
                    logger::info("[Dashboard] Action toggled: {} = {} in {}", name, enabled, yamlPath);
                }
            }
        
            // Push updated state so React reflects the change
            mgr->SendModEvent("IntelEngine_DashboardRefresh");
        } catch (...) {}
    }

    // =========================================================================
    // Action YAML Parser (cached, lightweight)
    // =========================================================================

    void DashboardUIManager::LoadActionYAMLs() {
        actionCache_.clear();
        actionCacheLoaded_ = true;

        std::string dir = kActionYamlDirectory;
        if (!std::filesystem::exists(dir)) {
            logger::warn("[Dashboard] Action YAML directory not found: {}", dir);
            return;
        }

        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto wext = entry.path().extension().wstring();
            if (wext != L".yaml" && wext != L".yml") continue;

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            ActionMeta meta;
            try {
                meta.fileName = entry.path().stem().string();
            } catch (...) {
                continue; // skip files with non-ANSI names
            }

            std::string line;
            bool inParamBlock = false;
            bool inDescription = false;
            ActionParam currentParam;
            bool hasCurrentParam = false;
            std::string currentParamType;

            while (std::getline(file, line)) {
                // Top-level fields (no leading whitespace)
                if (!line.empty() && line[0] != ' ' && line[0] != '-') {
                    inParamBlock = false;
                    inDescription = false;
                    if (hasCurrentParam && currentParamType == "dynamic") {
                        meta.params.push_back(currentParam);
                    }
                    hasCurrentParam = false;

                    if (line.rfind("name:", 0) == 0 && meta.name.empty()) {
                        meta.name = line.substr(5);
                        // Trim whitespace
                        auto s = meta.name.find_first_not_of(" \t");
                        meta.name = s != std::string::npos ? meta.name.substr(s) : "";
                    } else if (line.rfind("customCategory:", 0) == 0) {
                        auto s = line.find_first_not_of(" \t", 15);
                        meta.category = s != std::string::npos ? line.substr(s) : "";
                    } else if (line.rfind("executionFunctionName:", 0) == 0) {
                        meta.isCategory = false;
                    } else if (line.rfind("description:", 0) == 0) {
                        inDescription = true;
                        std::string val = line.substr(12);
                        auto s = val.find_first_not_of(" \t>");
                        if (s != std::string::npos) {
                            meta.description = val.substr(s);
                        }
                    } else if (line.rfind("enabled:", 0) == 0) {
                        std::string val = line.substr(8);
                        auto s = val.find_first_not_of(" \t");
                        val = s != std::string::npos ? val.substr(s) : "";
                        meta.enabled = (val == "true");
                    } else if (line.rfind("parameterMapping:", 0) == 0) {
                        inParamBlock = true;
                    }
                    continue;
                }

                // Grab first line of multi-line description
                if (inDescription && meta.description.empty()) {
                    auto s = line.find_first_not_of(" \t");
                    if (s != std::string::npos) {
                        meta.description = line.substr(s);
                        inDescription = false;
                    }
                    continue;
                }

                // Parse parameterMapping entries
                if (inParamBlock) {
                    auto trimmed = line;
                    auto ts = trimmed.find_first_not_of(" \t");
                    if (ts != std::string::npos) trimmed = trimmed.substr(ts);

                    if (trimmed.rfind("- type:", 0) == 0) {
                        // Save previous param
                        if (hasCurrentParam && currentParamType == "dynamic") {
                            meta.params.push_back(currentParam);
                        }
                        currentParam = {};
                        hasCurrentParam = true;
                        std::string val = trimmed.substr(7);
                        auto vs = val.find_first_not_of(" \t");
                        currentParamType = vs != std::string::npos ? val.substr(vs) : "";
                    } else if (trimmed.rfind("name:", 0) == 0 && hasCurrentParam) {
                        std::string val = trimmed.substr(5);
                        auto vs = val.find_first_not_of(" \t");
                        currentParam.name = vs != std::string::npos ? val.substr(vs) : "";
                    } else if (trimmed.rfind("enum:", 0) == 0 && hasCurrentParam) {
                        auto start = trimmed.find('[');
                        auto end = trimmed.find(']', start);
                        if (start != std::string::npos && end != std::string::npos) {
                            std::stringstream values(trimmed.substr(start + 1, end - start - 1));
                            std::string value;
                            while (std::getline(values, value, ',')) {
                                auto first = value.find_first_not_of(" \t\"'");
                                auto last = value.find_last_not_of(" \t\"'");
                                if (first != std::string::npos) currentParam.enumValues.push_back(value.substr(first, last - first + 1));
                            }
                        }
                    } else if (trimmed.rfind("description:", 0) == 0 && hasCurrentParam) {
                        std::string val = trimmed.substr(12);
                        auto vs = val.find_first_not_of(" \t>");
                        if (vs != std::string::npos) {
                            currentParam.description = val.substr(vs);
                        }
                    }
                }
            }

            // Don't forget the last param
            if (hasCurrentParam && currentParamType == "dynamic") {
                meta.params.push_back(currentParam);
            }

            if (!meta.name.empty()) {
                actionCache_.push_back(std::move(meta));
            }
        }

        logger::info("[Dashboard] Loaded {} action YAMLs", actionCache_.size());
    }

    // =========================================================================
    // View Reload
    // =========================================================================

    void DashboardUIManager::ReloadView() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shuttingDown_ || !host_ || !available_.load()) return;
            ++generation_;
            pendingReload_ = true;
        }
        pollCondition_.notify_one();
    }

}  // namespace IntelEngine
