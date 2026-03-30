/**
 * Dashboard UI Manager Implementation
 *
 * PrismaUI integration for the IntelEngine dashboard.
 * Hotkey polling runs on a background thread (50ms interval).
 *
 * Data flow:
 *   Papyrus -> PushFullState(json) -> InteropCall("updateFullState", json) -> React
 *   React -> onDashboard_* listeners -> C++ -> SKSE ModEvent -> Papyrus
 */

#include "DashboardUIManager.h"
#include "DashboardConfig.h"
#include "SkyrimNetAPI.h"
#include "SlotTracker.h"
#include "ProcessUtils.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace IntelEngine {

    // Static member definitions
    std::unordered_map<std::string, std::string> DashboardUIManager::pendingParams_;
    std::mutex DashboardUIManager::pendingParamsMutex_;

    std::string DashboardUIManager::GetPendingParam(const std::string& key) {
        std::lock_guard<std::mutex> lock(pendingParamsMutex_);
        auto it = pendingParams_.find(key);
        return it != pendingParams_.end() ? it->second : "";
    }

    void DashboardUIManager::ClearPendingParams() {
        std::lock_guard<std::mutex> lock(pendingParamsMutex_);
        pendingParams_.clear();
    }

    std::string DashboardUIManager::ClaimPendingParams() {
        std::lock_guard<std::mutex> lock(pendingParamsMutex_);
        if (pendingParams_.empty()) return "";
        nlohmann::json j;
        for (auto& [key, val] : pendingParams_) {
            j[key] = val;
        }
        pendingParams_.clear();
        return j.dump();
    }

    static constexpr int kViewRenderOrder = 90;
    static constexpr auto kPollIntervalMs = std::chrono::milliseconds(50);
    static constexpr auto kKeyPressCooldownMs = std::chrono::milliseconds(300);

    // =========================================================================
    // Lifecycle
    // =========================================================================

    void DashboardUIManager::Initialize() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (initialized_) return;
        initialized_ = true;

        try {
            auto* api = static_cast<PRISMA_UI_API::IVPrismaUI1*>(
                PRISMA_UI_API::RequestPluginAPI(PRISMA_UI_API::InterfaceVersion::V1));
            if (api) {
                prismaUI_ = api;
                logger::info("[Dashboard] PrismaUI API acquired");
                CreateViewIfNeeded();
            }
        } catch (...) {
            logger::warn("[Dashboard] Failed to acquire PrismaUI API");
        }

        if (!prismaUI_) {
            logger::info("[Dashboard] PrismaUI not available - dashboard disabled");
            return;
        }

        stopPolling_.store(false);
        hotkeyThread_ = std::thread(&DashboardUIManager::HotkeyPollLoop, this);
        logger::info("[Dashboard] Hotkey polling thread started");
    }

    void DashboardUIManager::Shutdown() {
        stopPolling_.store(true);
        if (hotkeyThread_.joinable()) hotkeyThread_.join();

        std::lock_guard<std::mutex> lock(mutex_);
        if (prismaUI_ && dashboardView_ != 0) {
            try { prismaUI_->Destroy(dashboardView_); } catch (...) {}
            dashboardView_ = 0;
        }
        isOpen_.store(false);
        domReady_.store(false);
    }

    void DashboardUIManager::Toggle() {
        if (isOpen_.load()) Hide();
        else Show();
    }

    void DashboardUIManager::Show() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!prismaUI_ || isOpen_.load()) return;
        if (!CreateViewIfNeeded()) return;

        isOpen_.store(true);
        prismaUI_->Show(dashboardView_);
        logger::info("[Dashboard] Show: domReady={}", domReady_.load());

        if (domReady_.load()) {
            prismaUI_->Focus(dashboardView_, true, false);
            // Request full state from Papyrus via ModEvent
            SendModEvent("IntelEngine_DashboardOpened");
            // Also push slot data as fallback
            PushSlotData();
        }
    }

    void DashboardUIManager::Hide() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!prismaUI_ || dashboardView_ == 0 || !isOpen_.load()) return;

        try {
            prismaUI_->Unfocus(dashboardView_);
            prismaUI_->Hide(dashboardView_);
        } catch (...) {}

        isOpen_.store(false);
    }

    bool DashboardUIManager::CreateViewIfNeeded() {
        if (dashboardView_ != 0 && prismaUI_->IsValid(dashboardView_)) return true;

        // Don't check std::filesystem::exists — MO2's VFS makes the file visible
        // to PrismaUI but not to the real filesystem. Let CreateView handle resolution.

        domReady_.store(false);

        try {
            const char* viewPath = "IntelEngine/dashboard/index.html";
            dashboardView_ = prismaUI_->CreateView(viewPath, OnDomReadyStatic);
            if (dashboardView_ == 0) return false;

            prismaUI_->SetOrder(dashboardView_, kViewRenderOrder);
            prismaUI_->Hide(dashboardView_);
            return true;
        } catch (...) {
            dashboardView_ = 0;
            return false;
        }
    }

    // =========================================================================
    // Data Push (C++ -> JS)
    // =========================================================================

    void DashboardUIManager::PushSlotData() {
        if (!prismaUI_ || dashboardView_ == 0 || !domReady_.load()) return;
        try {
            std::string json = SlotTracker::GetSingleton()->SerializeToJson();
            prismaUI_->InteropCall(dashboardView_, "updateSlots", json.c_str());
        } catch (const std::exception& e) {
            logger::error("[Dashboard] Failed to push slot data: {}", e.what());
        }
    }

    void DashboardUIManager::PushFullState(const std::string& json) {
        if (!prismaUI_ || dashboardView_ == 0 || !domReady_.load()) {
            logger::warn("[Dashboard] PushFullState: skipped (prisma={}, view={}, domReady={})",
                        prismaUI_ != nullptr, dashboardView_, domReady_.load());
            return;
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

            // Inject plugin config values from settings.yaml (read via SkyrimNet API)
            if (SkyrimNetAPI::GetPluginConfigValue) {
                auto rv = [](const char* p, const char* d) -> std::string {
                    return SkyrimNetAPI::GetPluginConfigValue("IntelEngine", p, d);
                };
                nlohmann::json pc;
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
                parsed["pluginConfig"] = pc;
            }

            // Inject loaded NPCs for Director tab (same cell as player only)
            {
                nlohmann::json npcs = nlohmann::json::array();
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* playerCell = player ? player->GetParentCell() : nullptr;
                ProcessUtils::ForEachLoadedActor([&](RE::Actor* actor) {
                    if (!actor || actor == player) return false;
                    if (actor->IsDead() || actor->IsDisabled()) return false;
                    if (!actor->Is3DLoaded()) return false;
                    if (actor->GetParentCell() != playerCell) return false;
                    auto* base = actor->GetActorBase();
                    if (!base) return false;
                    if (!base->IsUnique()) return false;
                    nlohmann::json entry;
                    entry["formId"] = actor->GetFormID();
                    entry["name"] = actor->GetDisplayFullName();
                    npcs.push_back(entry);
                    return false;
                });
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
                    entry["enabled"] = a.enabled;
                    nlohmann::json params = nlohmann::json::array();
                    for (auto& p : a.params) {
                        params.push_back({{"name", p.name}, {"description", p.description}});
                    }
                    entry["params"] = params;
                    acts.push_back(entry);
                }
                parsed["actions"] = acts;
            }

            std::string clean = parsed.dump();
            logger::debug("[Dashboard] PushFullState: sending {} bytes", clean.size());
            prismaUI_->InteropCall(dashboardView_, "updateFullState", clean.c_str());
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
            std::this_thread::sleep_for(kPollIntervalMs);

            auto* config = DashboardConfig::GetSingleton();
            int vk = config->GetHotkey();
            int mods = config->GetModifiers();
            if (vk < 0 || vk > 255) continue;

            auto* ui = RE::UI::GetSingleton();
            if (ui && ui->GameIsPaused() && !isOpen_.load()) continue;

            bool modifiersOk = true;
            if (mods & kModCtrl)  modifiersOk = modifiersOk && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
            if (mods & kModShift) modifiersOk = modifiersOk && (GetAsyncKeyState(VK_SHIFT) & 0x8000);
            if (mods & kModAlt)   modifiersOk = modifiersOk && (GetAsyncKeyState(VK_MENU) & 0x8000);

            bool mainKeyDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
            bool comboPressed = modifiersOk && mainKeyDown;

            if (comboPressed && !wasComboPressed) {
                auto now = std::chrono::steady_clock::now();
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
    // Static Callbacks (PrismaUI -> Instance)
    // =========================================================================

    void DashboardUIManager::OnDomReadyStatic(PrismaView view) {
        DashboardUIManager::GetSingleton()->HandleDomReady(view);
    }

    void DashboardUIManager::OnCloseStatic(const char*) {
        auto* task = SKSE::GetTaskInterface();
        if (task) task->AddTask([]() { DashboardUIManager::GetSingleton()->Hide(); });
    }

    void DashboardUIManager::OnRequestRefreshStatic(const char*) {
        auto* task = SKSE::GetTaskInterface();
        if (task) task->AddTask([]() {
            DashboardUIManager::GetSingleton()->SendModEvent("IntelEngine_DashboardRefresh");
            DashboardUIManager::GetSingleton()->PushSlotData();
        });
    }

    void DashboardUIManager::OnReloadUIStatic(const char*) {
        auto* task = SKSE::GetTaskInterface();
        if (task) task->AddTask([]() { DashboardUIManager::GetSingleton()->ReloadView(); });
    }

    // Dashboard action callbacks from JS
    void DashboardUIManager::OnCancelTaskStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                int slot = j.value("slot", -1);
                if (slot >= 0) {
                    DashboardUIManager::GetSingleton()->SendModEvent(
                        "IntelEngine_DashboardCancelTask", "", static_cast<float>(slot));
                }
            } catch (...) {}
        });
    }

    void DashboardUIManager::OnCancelScheduleStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                int slot = j.value("slot", -1);
                if (slot >= 0) {
                    DashboardUIManager::GetSingleton()->SendModEvent(
                        "IntelEngine_DashboardCancelSchedule", "", static_cast<float>(slot));
                }
            } catch (...) {}
        });
    }

    void DashboardUIManager::OnToggleStoryTypeStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                std::string type = j.value("type", "");
                bool enabled = j.value("enabled", true);
                if (!type.empty()) {
                    DashboardUIManager::GetSingleton()->SendModEvent(
                        "IntelEngine_DashboardToggleStory", type, enabled ? 1.0f : 0.0f);
                }
            } catch (...) {}
        });
    }

    void DashboardUIManager::OnChangeSettingStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
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
        });
    }

    void DashboardUIManager::OnRemovePackagesStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                int formId = j.value("formId", 0);
                if (formId > 0) {
                    DashboardUIManager::GetSingleton()->SendModEvent(
                        "IntelEngine_DashboardRemovePackages", "", static_cast<float>(formId));
                }
            } catch (...) {}
        });
    }

    void DashboardUIManager::OnChangePluginConfigStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
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
                    yamlValue = val.dump();
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

                if (config->WriteYamlValue(section, key, yamlValue)) {
                    logger::info("[Dashboard] Plugin config updated: {}.{} = {}", section, key, yamlValue);
                    DashboardUIManager::GetSingleton()->SendModEvent("IntelEngine_DashboardRefresh");
                } else {
                    logger::warn("[Dashboard] Failed to write plugin config: {}.{}", section, key);
                }
            } catch (...) {}
        });
    }

    // =========================================================================
    // Instance Handlers
    // =========================================================================

    void DashboardUIManager::HandleDomReady(PrismaView) {
        domReady_.store(true);

        // Register JS -> C++ listeners (safe from PrismaUI thread)
        prismaUI_->RegisterJSListener(dashboardView_, "onCloseDashboard", OnCloseStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onRequestRefresh", OnRequestRefreshStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onReloadUI", OnReloadUIStatic);

        // Dashboard action listeners
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_cancelTask", OnCancelTaskStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_cancelSchedule", OnCancelScheduleStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_toggleStoryType", OnToggleStoryTypeStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_changeSetting", OnChangeSettingStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_removePackages", OnRemovePackagesStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_changePluginConfig", OnChangePluginConfigStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_dispatchStory", OnDispatchStoryStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_dispatchNpcSocial", OnDispatchNpcSocialStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_dispatchPolitics", OnDispatchPoliticsStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_executeAction", OnExecuteActionStatic);
        prismaUI_->RegisterJSListener(dashboardView_, "onDashboard_toggleAction", OnToggleActionStatic);

        logger::info("[Dashboard] JS listeners registered");

        if (isOpen_.load()) {
            // Focus is a PrismaUI call — safe from this thread
            prismaUI_->Focus(dashboardView_, true, false);

            // SKSE events and SlotTracker MUST run on the game thread
            auto* task = SKSE::GetTaskInterface();
            if (task) {
                task->AddTask([]() {
                    auto* mgr = DashboardUIManager::GetSingleton();
                    mgr->SendModEvent("IntelEngine_DashboardOpened");
                    mgr->PushSlotData();
                });
            }
        }
    }

    void DashboardUIManager::HandleClose() {
        auto* task = SKSE::GetTaskInterface();
        if (task) task->AddTask([]() { DashboardUIManager::GetSingleton()->Hide(); });
    }

    void DashboardUIManager::HandleRequestRefresh() {
        auto* task = SKSE::GetTaskInterface();
        if (task) task->AddTask([]() {
            DashboardUIManager::GetSingleton()->SendModEvent("IntelEngine_DashboardRefresh");
            DashboardUIManager::GetSingleton()->PushSlotData();
        });
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

    // =========================================================================
    // Director: Story Dispatch (JS -> C++ -> pending params -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnDispatchStoryStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                std::string npcName = j.value("npcName", "");
                std::string type = j.value("storyType", "");
                std::string narration = j.value("narration", "");
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
                eventPayload["response"] = response.dump();

                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardDispatchStory", eventPayload.dump(), 0.0f);
                logger::info("[Dashboard] Director: dispatch story type={} npc={}", type, npcName);
            } catch (const std::exception& e) {
                logger::error("[Dashboard] Director story dispatch failed: {}", e.what());
            } catch (...) {
                logger::error("[Dashboard] Director story dispatch failed: unknown error");
            }
        });
    }

    // =========================================================================
    // Director: NPC Social Dispatch (JS -> C++ -> pending params -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnDispatchNpcSocialStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                std::string npc1 = j.value("npc1Name", "");
                std::string npc2 = j.value("npc2Name", "");
                std::string type = j.value("socialType", "");
                std::string narration = j.value("narration", "");
                if (npc1.empty() || npc2.empty() || type.empty() || narration.empty()) return;

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
                eventPayload["response"] = response.dump();

                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardDispatchNpcSocial", eventPayload.dump(), 0.0f);
                logger::info("[Dashboard] Director: NPC social type={} npc1={} npc2={}", type, npc1, npc2);
            } catch (const std::exception& e) {
                logger::error("[Dashboard] Director NPC social dispatch failed: {}", e.what());
            } catch (...) {
                logger::error("[Dashboard] Director NPC social dispatch failed: unknown error");
            }
        });
    }

    // =========================================================================
    // Director: Political Event Dispatch (JS -> C++ -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnDispatchPoliticsStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                std::string factionA = j.value("factionA", "");
                std::string factionB = j.value("factionB", "");
                std::string eventType = j.value("eventType", "");
                std::string description = j.value("description", "");
                int delta = j.value("relationDelta", 0);
                if (factionA.empty() || factionB.empty() || eventType.empty() || description.empty() || delta == 0) return;

                // Build a fake Political DM response matching the expected JSON format
                nlohmann::json response;
                response["should_act"] = true;
                response["faction_a"] = factionA;
                response["faction_b"] = factionB;
                response["event_type"] = eventType;
                response["description"] = description;
                response["relation_delta"] = delta;

                // Pass the full response JSON as strArg — Papyrus feeds it to ProcessPoliticalDMResponse
                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardDispatchPolitics", response.dump(), 0.0f);
                logger::info("[Dashboard] Director: politics event={} factionA={} factionB={} delta={}",
                    eventType, factionA, factionB, delta);
            } catch (const std::exception& e) {
                logger::error("[Dashboard] Director politics dispatch failed: {}", e.what());
            } catch (...) {
                logger::error("[Dashboard] Director politics dispatch failed: unknown error");
            }
        });
    }

    // =========================================================================
    // Director: Action Execution (JS -> C++ -> pending params -> ModEvent -> Papyrus)
    // =========================================================================

    void DashboardUIManager::OnExecuteActionStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
            try {
                auto j = nlohmann::json::parse(arg);
                int formId = j.value("npcFormId", 0);
                std::string actionName = j.value("actionName", "");
                if (formId <= 0 || actionName.empty()) return;

                // Store all action params for Papyrus retrieval
                {
                    std::lock_guard<std::mutex> lock(pendingParamsMutex_);
                    pendingParams_.clear();
                    pendingParams_["actionName"] = actionName;
                    if (j.contains("params") && j["params"].is_object()) {
                        for (auto& [key, val] : j["params"].items()) {
                            pendingParams_[key] = val.is_string() ? val.get<std::string>() : val.dump();
                        }
                    }
                }

                // Pass ALL params as JSON in strArg (same race fix)
                nlohmann::json eventPayload;
                {
                    std::lock_guard<std::mutex> lock(pendingParamsMutex_);
                    for (auto& [k, v] : pendingParams_) {
                        eventPayload[k] = v;
                    }
                }
                DashboardUIManager::GetSingleton()->SendModEvent(
                    "IntelEngine_DashboardExecuteAction", eventPayload.dump(), static_cast<float>(formId));
                logger::info("[Dashboard] Director: execute action={} npc=0x{:X}", actionName, formId);
            } catch (...) {}
        });
    }

    // =========================================================================
    // Actions Tab: Toggle enabled in YAML (persistent, restart required)
    // =========================================================================

    void DashboardUIManager::OnToggleActionStatic(const char* jsonArg) {
        auto* task = SKSE::GetTaskInterface();
        if (!task) return;
        std::string arg(jsonArg ? jsonArg : "{}");
        task->AddTask([arg]() {
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
                std::string yamlPath = "Data/SKSE/Plugins/SkyrimNet/config/actions/" + fileName + ".yaml";
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
        });
    }

    // =========================================================================
    // Action YAML Parser (cached, lightweight)
    // =========================================================================

    void DashboardUIManager::LoadActionYAMLs() {
        actionCache_.clear();
        actionCacheLoaded_ = true;

        std::string dir = "Data/SKSE/Plugins/SkyrimNet/config/actions";
        if (!std::filesystem::exists(dir)) {
            logger::warn("[Dashboard] Action YAML directory not found: {}", dir);
            return;
        }

        for (auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".yaml" && ext != ".yml") continue;

            std::ifstream file(entry.path());
            if (!file.is_open()) continue;

            ActionMeta meta;
            meta.fileName = entry.path().stem().string();

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
        DashboardConfig::GetSingleton()->Reload();
        {
            std::lock_guard<std::mutex> cacheLock(actionCacheMutex_);
            actionCacheLoaded_ = false;  // Force re-read on next push
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (!prismaUI_) return;

        bool wasOpen = isOpen_.load();

        if (dashboardView_ != 0) {
            try {
                prismaUI_->Unfocus(dashboardView_);
                prismaUI_->Destroy(dashboardView_);
            } catch (...) {}
            dashboardView_ = 0;
        }

        isOpen_.store(false);
        domReady_.store(false);

        if (!CreateViewIfNeeded()) {
            logger::error("[Dashboard] Reload failed");
            return;
        }

        logger::info("[Dashboard] View reloaded from disk");

        if (wasOpen) {
            isOpen_.store(true);
            prismaUI_->Show(dashboardView_);
        }
    }

}  // namespace IntelEngine
