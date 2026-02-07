#pragma once

/**
 * SkyrimNet C++ API - Local copy for IntelEngine
 *
 * Provides function pointers to SkyrimNet's public API for:
 * - Decorator registration (C++ decorators for synchronous eligibility checks)
 * - Eligibility tag registration (custom tags for YAML eligibilityRules)
 *
 * Usage:
 *   Call SkyrimNetAPI::Initialize() after kDataLoaded.
 *   Check individual function pointers for null before calling.
 *
 * Based on SkyrimNet Public API v3.
 */

#include <string>
#include <functional>
#include <windows.h>

namespace RE { class Actor; }

namespace IntelEngine::SkyrimNetAPI {

    // Callback types
    using DecoratorCallback = std::function<std::string(RE::Actor*)>;
    using EligibilityCallback = std::function<bool(RE::Actor*)>;

    // Function pointers (resolved at runtime)
    inline int (*GetVersion)() = nullptr;
    inline int (*DecoratorRegister)(const std::string& id, DecoratorCallback callback) = nullptr;
    inline int (*TagRegister)(const std::string& name, EligibilityCallback callback) = nullptr;
    inline bool (*DecoratorExists)(const std::string& id) = nullptr;

    /**
     * Initialize the SkyrimNet API by loading function pointers from the DLL.
     * Returns true if SkyrimNet was found and API version is >= 3.
     * Safe to call even if SkyrimNet is not installed (returns false).
     */
    inline bool Initialize() {
        auto hDLL = LoadLibraryA("SkyrimNet");
        if (!hDLL) {
            logger::info("SkyrimNet DLL not found — decorators/tags will not be registered");
            return false;
        }

        GetVersion = reinterpret_cast<int(*)()>(
            GetProcAddress(hDLL, "PublicGetVersion"));

        if (!GetVersion) {
            logger::warn("SkyrimNet found but PublicGetVersion not exported — old version?");
            return false;
        }

        int version = GetVersion();
        logger::info("SkyrimNet API v{} detected", version);

        if (version < 3) {
            logger::warn("SkyrimNet API v3+ required for decorators/tags (found v{})", version);
            return false;
        }

        // v3+ function pointers
        DecoratorRegister = reinterpret_cast<int(*)(const std::string&, DecoratorCallback)>(
            GetProcAddress(hDLL, "PublicDecoratorRegister"));

        TagRegister = reinterpret_cast<int(*)(const std::string&, EligibilityCallback)>(
            GetProcAddress(hDLL, "PublicTagRegister"));

        DecoratorExists = reinterpret_cast<bool(*)(const std::string&)>(
            GetProcAddress(hDLL, "PublicDecoratorExists"));

        logger::info("SkyrimNet API initialized: DecoratorRegister={}, TagRegister={}, DecoratorExists={}",
                     DecoratorRegister != nullptr, TagRegister != nullptr, DecoratorExists != nullptr);

        return true;
    }

}  // namespace IntelEngine::SkyrimNetAPI
