#pragma once

/**
 * Settings Manager
 *
 * Loads and stores plugin settings from INI file.
 */

#include "Plugin.h"

namespace IntelEngine {

    class Settings {
    public:
        static Settings* GetSingleton() {
            static Settings instance;
            return &instance;
        }

        /**
         * Load settings from INI file.
         */
        void Load();

        /**
         * Save settings to INI file.
         */
        void Save();

        /**
         * Apply spdlog level from an integer debug level.
         * Shared by Settings::Load() and Papyrus SetDebugLevel().
         */
        static void ApplyDebugLevel(int level) {
            switch (level) {
                case 0: spdlog::set_level(spdlog::level::off); break;
                case 1: spdlog::set_level(spdlog::level::err); break;
                case 2: spdlog::set_level(spdlog::level::warn); break;
                case 3: spdlog::set_level(spdlog::level::info); break;
                case 4: spdlog::set_level(spdlog::level::debug); break;
                default: spdlog::set_level(spdlog::level::info); break;
            }
        }

        // Settings values
        int debugLevel = 3;           // 0=off, 1=errors, 2=warn, 3=info, 4=debug
        int fuzzyMatchThreshold = 3;  // Levenshtein distance threshold

        // Stuck detection settings
        int stuckMaxChecks = 3;       // Consecutive stuck checks before recovery
        int stuckMaxRecovery = 3;     // Recovery cycles before teleport

        // Departure detection settings
        int departureMinChecks = 5;   // Ticks before first distance check (~15s at 3s interval)
        int departureMaxRetries = 1;  // Soft recovery attempts before escalation

    private:
        Settings() = default;
        ~Settings() = default;
        Settings(const Settings&) = delete;
        Settings& operator=(const Settings&) = delete;

        std::string GetConfigPath();
    };

}  // namespace IntelEngine
