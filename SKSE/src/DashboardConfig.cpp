/**
 * Dashboard Configuration Implementation
 *
 * Reads/writes ui.dashboard_hotkey and ui.dashboard_modifiers from settings.yaml.
 * Uses simple line-based YAML parsing (no yaml-cpp dependency).
 */

#include "DashboardConfig.h"
#include "SkyrimNetAPI.h"

#include <fstream>
#include <filesystem>
#include <string>
#include <vector>

namespace IntelEngine {

    std::string DashboardConfig::GetSettingsPath() {
        return "Data/SKSE/Plugins/SkyrimNet/config/plugins/IntelEngine/settings.yaml";
    }

    // Read an integer value from a YAML file under a given section.key
    int DashboardConfig::ReadYamlInt(const std::string& path, const std::string& section,
                                      const std::string& key, int defaultVal) {
        std::ifstream file(path);
        if (!file.is_open()) return defaultVal;

        bool inSection = false;
        std::string line;
        std::string keyPrefix = key + ":";

        while (std::getline(file, line)) {
            size_t firstNonSpace = line.find_first_not_of(" \t");
            if (firstNonSpace == std::string::npos) continue;

            std::string trimmed = line.substr(firstNonSpace);

            if (firstNonSpace == 0) {
                inSection = (trimmed.rfind(section + ":", 0) == 0);
                continue;
            }

            if (inSection && trimmed.rfind(keyPrefix, 0) == 0) {
                std::string valueStr = trimmed.substr(keyPrefix.length());
                size_t valStart = valueStr.find_first_not_of(" \t");
                if (valStart == std::string::npos) return defaultVal;
                valueStr = valueStr.substr(valStart);

                size_t commentPos = valueStr.find('#');
                if (commentPos != std::string::npos) valueStr = valueStr.substr(0, commentPos);

                size_t valEnd = valueStr.find_last_not_of(" \t\r\n");
                if (valEnd != std::string::npos) valueStr = valueStr.substr(0, valEnd + 1);

                try { return std::stoi(valueStr); }
                catch (...) { return defaultVal; }
            }
        }
        return defaultVal;
    }

    // Read a string value from a YAML file under a given section.key
    std::string DashboardConfig::ReadYamlString(const std::string& path, const std::string& section,
                                                 const std::string& key, const std::string& defaultVal) {
        std::ifstream file(path);
        if (!file.is_open()) return defaultVal;

        bool inSection = false;
        std::string line;
        std::string keyPrefix = key + ":";

        while (std::getline(file, line)) {
            size_t firstNonSpace = line.find_first_not_of(" \t");
            if (firstNonSpace == std::string::npos) continue;

            std::string trimmed = line.substr(firstNonSpace);

            if (firstNonSpace == 0) {
                inSection = (trimmed.rfind(section + ":", 0) == 0);
                continue;
            }

            if (inSection && trimmed.rfind(keyPrefix, 0) == 0) {
                std::string valueStr = trimmed.substr(keyPrefix.length());
                size_t valStart = valueStr.find_first_not_of(" \t");
                if (valStart == std::string::npos) return defaultVal;
                valueStr = valueStr.substr(valStart);

                // Strip trailing comment and whitespace
                size_t commentPos = valueStr.find('#');
                if (commentPos != std::string::npos) valueStr = valueStr.substr(0, commentPos);
                size_t valEnd = valueStr.find_last_not_of(" \t\r\n");
                if (valEnd != std::string::npos) valueStr = valueStr.substr(0, valEnd + 1);
                else return defaultVal;

                // Strip surrounding quotes
                if (valueStr.size() >= 2 &&
                    ((valueStr.front() == '"' && valueStr.back() == '"') ||
                     (valueStr.front() == '\'' && valueStr.back() == '\''))) {
                    valueStr = valueStr.substr(1, valueStr.size() - 2);
                }

                return valueStr;
            }
        }
        return defaultVal;
    }

    // General-purpose YAML value writer (formattedValue is already YAML-ready)
    bool DashboardConfig::WriteYamlValue(const std::string& section,
                                          const std::string& key,
                                          const std::string& formattedValue) {
        std::lock_guard<std::mutex> lock(writeMutex_);
        std::string path = GetSettingsPath();
        if (!std::filesystem::exists(path)) {
            logger::warn("DashboardConfig: Cannot save - settings.yaml not found at {}", path);
            return false;
        }

        std::ifstream inFile(path);
        if (!inFile.is_open()) return false;

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(inFile, line)) lines.push_back(line);
        inFile.close();

        bool inSection = false;
        bool found = false;
        std::string keyPrefix = key + ":";

        for (size_t i = 0; i < lines.size(); ++i) {
            size_t firstNonSpace = lines[i].find_first_not_of(" \t");
            if (firstNonSpace == std::string::npos) continue;

            std::string trimmed = lines[i].substr(firstNonSpace);

            if (firstNonSpace == 0) {
                inSection = (trimmed.rfind(section + ":", 0) == 0);
                continue;
            }

            if (inSection && trimmed.rfind(keyPrefix, 0) == 0) {
                std::string indent = lines[i].substr(0, firstNonSpace);
                lines[i] = indent + key + ": " + formattedValue;
                found = true;
                break;
            }
        }

        if (!found) {
            bool hasSection = false;
            size_t sectionIdx = 0;
            for (size_t i = 0; i < lines.size(); ++i) {
                if (lines[i].find_first_not_of(" \t") == 0 &&
                    lines[i].rfind(section + ":", 0) == 0) {
                    hasSection = true;
                    sectionIdx = i;
                    break;
                }
            }
            if (hasSection) {
                lines.insert(lines.begin() + sectionIdx + 1,
                             "  " + key + ": " + formattedValue);
            } else {
                lines.push_back(section + ":");
                lines.push_back("  " + key + ": " + formattedValue);
            }
        }

        std::ofstream outFile(path);
        if (!outFile.is_open()) return false;

        for (size_t i = 0; i < lines.size(); ++i) {
            outFile << lines[i];
            if (i + 1 < lines.size()) outFile << "\n";
        }
        return true;
    }

    // Convenience: write integer value
    bool DashboardConfig::WriteYamlInt(const std::string& section,
                                        const std::string& key, int value) {
        return WriteYamlValue(section, key, std::to_string(value));
    }

    void DashboardConfig::Load() {
        // Try SkyrimNet API first
        // Defaults: Shift+7 (VK_7=0x37=55, kModShift=2)
        constexpr int kDefaultHotkey = 55;     // VK_7
        constexpr int kDefaultModifiers = kModShift;

        if (SkyrimNetAPI::GetPluginConfigValue) {
            try {
                std::string hkVal = SkyrimNetAPI::GetPluginConfigValue(
                    "IntelEngine", "ui.dashboard_hotkey",
                    std::to_string(kDefaultHotkey).c_str());
                std::string modVal = SkyrimNetAPI::GetPluginConfigValue(
                    "IntelEngine", "ui.dashboard_modifiers",
                    std::to_string(kDefaultModifiers).c_str());
                dashboardHotkey_.store(std::stoi(hkVal));
                dashboardModifiers_.store(std::stoi(modVal));
                logger::info("DashboardConfig: Loaded via API - key={}, modifiers={}",
                             dashboardHotkey_.load(), dashboardModifiers_.load());
                return;
            } catch (const std::exception& e) {
                logger::warn("DashboardConfig: API parse failed: {}", e.what());
            }
        }

        // Fallback: read settings.yaml directly
        std::string path = GetSettingsPath();
        if (!std::filesystem::exists(path)) {
            logger::info("DashboardConfig: settings.yaml not found, using defaults (Shift+7)");
            return;
        }

        dashboardHotkey_.store(ReadYamlInt(path, "ui", "dashboard_hotkey", kDefaultHotkey));
        dashboardModifiers_.store(ReadYamlInt(path, "ui", "dashboard_modifiers", kDefaultModifiers));
        logger::info("DashboardConfig: Loaded from file - key={}, modifiers={}",
                     dashboardHotkey_.load(), dashboardModifiers_.load());
    }

    bool DashboardConfig::SetHotkey(int vkCode) {
        if (!WriteYamlInt("ui", "dashboard_hotkey", vkCode)) return false;
        dashboardHotkey_.store(vkCode);
        logger::info("DashboardConfig: Hotkey saved: {}", vkCode);
        return true;
    }

    bool DashboardConfig::SetModifiers(int modifiers) {
        if (!WriteYamlInt("ui", "dashboard_modifiers", modifiers)) return false;
        dashboardModifiers_.store(modifiers);
        logger::info("DashboardConfig: Modifiers saved: {}", modifiers);
        return true;
    }

}  // namespace IntelEngine
