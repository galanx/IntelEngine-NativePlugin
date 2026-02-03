/**
 * Settings Manager Implementation
 */

#include "Settings.h"

#include <fstream>
#include <filesystem>

namespace IntelEngine {

    std::string Settings::GetConfigPath() {
        auto path = logger::log_directory();
        if (path) {
            *path /= "IntelEngine.ini";
            return path->string();
        }
        return "Data/SKSE/Plugins/IntelEngine.ini";
    }

    void Settings::Load() {
        std::string configPath = GetConfigPath();

        if (!std::filesystem::exists(configPath)) {
            logger::info("No config file found at {}, using defaults", configPath);
            Save();  // Create default config
            return;
        }

        std::ifstream file(configPath);
        if (!file.is_open()) {
            logger::warn("Failed to open config file: {}", configPath);
            return;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(file, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            line = line.substr(start);

            // Skip comments
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            // Section header
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos) {
                    currentSection = line.substr(1, end - 1);
                }
                continue;
            }

            // Key=Value
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);

            // Trim
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));

            // Parse values (try/catch guards against malformed INI entries)
            try {
                if (currentSection == "General") {
                    if (key == "DebugLevel") {
                        debugLevel = std::stoi(value);
                    } else if (key == "FuzzyMatchThreshold") {
                        fuzzyMatchThreshold = std::stoi(value);
                    }
                } else if (currentSection == "StuckDetection") {
                    if (key == "MaxChecks") {
                        stuckMaxChecks = std::stoi(value);
                    } else if (key == "MaxRecovery") {
                        stuckMaxRecovery = std::stoi(value);
                    }
                } else if (currentSection == "DepartureDetection") {
                    if (key == "MinChecks") {
                        departureMinChecks = std::stoi(value);
                    } else if (key == "MaxRetries") {
                        departureMaxRetries = std::stoi(value);
                    }
                }
            } catch (const std::exception& e) {
                logger::warn("Failed to parse setting [{}] {}={}: {}", currentSection, key, value, e.what());
            }
        }

        // Apply debug level
        ApplyDebugLevel(debugLevel);

        logger::info("Settings loaded from {}", configPath);
    }

    void Settings::Save() {
        std::string configPath = GetConfigPath();

        std::ofstream file(configPath);
        if (!file.is_open()) {
            logger::error("Failed to create config file: {}", configPath);
            return;
        }

        file << "; IntelEngine Configuration\n";
        file << "; Intelligent NPC Task Execution for SkyrimNet\n\n";

        file << "[General]\n";
        file << "; Debug logging level: 0=off, 1=errors, 2=warnings, 3=info, 4=verbose\n";
        file << "DebugLevel=" << debugLevel << "\n";
        file << "; Maximum Levenshtein distance for fuzzy name matching\n";
        file << "FuzzyMatchThreshold=" << fuzzyMatchThreshold << "\n";

        file << "\n[StuckDetection]\n";
        file << "; Consecutive stuck checks before attempting recovery\n";
        file << "MaxChecks=" << stuckMaxChecks << "\n";
        file << "; Recovery cycles before teleporting NPC to destination\n";
        file << "MaxRecovery=" << stuckMaxRecovery << "\n";

        file << "\n[DepartureDetection]\n";
        file << "; Ticks before first departure distance check (~3s per tick)\n";
        file << "MinChecks=" << departureMinChecks << "\n";
        file << "; Soft recovery attempts before escalation\n";
        file << "MaxRetries=" << departureMaxRetries << "\n";

        logger::info("Settings saved to {}", configPath);
    }

}  // namespace IntelEngine
