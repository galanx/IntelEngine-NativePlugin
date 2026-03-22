#pragma once

/**
 * Dashboard Configuration
 *
 * Reads/writes the dashboard hotkey combo from the plugin's settings.yaml.
 * Single source of truth: config/plugins/IntelEngine/settings.yaml
 *
 * Supports modifier keys via bitmask: 1=Ctrl, 2=Shift, 4=Alt.
 * Default: Shift+7 (key=55, modifiers=2)
 */

#include "Plugin.h"
#include <mutex>
#include <atomic>

namespace IntelEngine {

    // Modifier bitmask flags
    static constexpr int kModCtrl  = 1;
    static constexpr int kModShift = 2;
    static constexpr int kModAlt   = 4;

    class DashboardConfig {
    public:
        static DashboardConfig* GetSingleton() {
            static DashboardConfig instance;
            return &instance;
        }

        void Load();
        void Reload() { Load(); }

        int GetHotkey() const { return dashboardHotkey_.load(); }
        int GetModifiers() const { return dashboardModifiers_.load(); }

        bool SetHotkey(int vkCode);
        bool SetModifiers(int modifiers);

        /** Write any YAML value by section.key. Value must be pre-formatted for YAML. */
        bool WriteYamlValue(const std::string& section, const std::string& key,
                            const std::string& formattedValue);

        /** Read a string value from settings.yaml. */
        std::string ReadYamlString(const std::string& path, const std::string& section,
                                    const std::string& key, const std::string& defaultVal);

    private:
        DashboardConfig() = default;
        ~DashboardConfig() = default;
        DashboardConfig(const DashboardConfig&) = delete;
        DashboardConfig& operator=(const DashboardConfig&) = delete;

        std::string GetSettingsPath();
        int ReadYamlInt(const std::string& path, const std::string& section,
                        const std::string& key, int defaultVal);
        bool WriteYamlInt(const std::string& section, const std::string& key, int value);

        std::atomic<int> dashboardHotkey_{55};     // VK_7 default (Shift+7)
        std::atomic<int> dashboardModifiers_{kModShift};  // Shift modifier
        std::mutex writeMutex_;
    };

}  // namespace IntelEngine
