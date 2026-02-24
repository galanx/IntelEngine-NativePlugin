#pragma once

/**
 * SkyrimNet C++ API - Local copy for IntelEngine
 *
 * Provides function pointers to SkyrimNet's public API for:
 * - Decorator registration (C++ decorators for synchronous eligibility checks)
 * - Eligibility tag registration (custom tags for YAML eligibilityRules)
 * - Data API (memories, events, dialogue, analytics — replaces direct SQLite access)
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

    // ---- Core ----
    inline int (*GetVersion)() = nullptr;

    // ---- Decorator/Tag Registration (v3+) ----
    inline int (*DecoratorRegister)(const std::string& id, DecoratorCallback callback) = nullptr;
    inline int (*TagRegister)(const std::string& name, EligibilityCallback callback) = nullptr;
    inline bool (*DecoratorExists)(const std::string& id) = nullptr;

    // ---- Bio Template (v3+) ----

    /** Get the bio template name for an actor (used for bio prompt file lookup). */
    inline std::string (*GetBioTemplateName)(uint32_t formId) = nullptr;

    // ---- Data API (v3+) ----

    /** Retrieve memories for an actor. contextQuery enables semantic search if non-empty. */
    inline std::string (*GetMemoriesForActor)(uint32_t formId, int maxCount, const char* contextQuery) = nullptr;

    /** Retrieve recent events, optionally filtered by actor and event type. */
    inline std::string (*GetRecentEvents)(uint32_t formId, int maxCount, const char* eventTypeFilter) = nullptr;

    /** Retrieve recent dialogue between the player and a specific NPC. */
    inline std::string (*GetRecentDialogue)(uint32_t formId, int maxExchanges) = nullptr;

    /** Get the most recent NPC who spoke to the player. */
    inline std::string (*GetLatestDialogueInfo)() = nullptr;

    /** Check if the memory/database system is ready. */
    inline bool (*IsMemorySystemReady)() = nullptr;

    /** Get per-actor engagement statistics (memory + event activity) for caller-side scoring.
     *  shortWindowSeconds/mediumWindowSeconds define recency buckets (e.g., 86400=24h, 604800=7d). */
    inline std::string (*GetActorEngagement)(int maxCount, bool excludePlayer, bool playerEventsOnly, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;

    /** Get actors related to a given actor via shared event history.
     *  shortWindowSeconds/mediumWindowSeconds define recency buckets (e.g., 86400=24h, 604800=7d). */
    inline std::string (*GetRelatedActors)(uint32_t formId, int maxCount, double shortWindowSeconds, double mediumWindowSeconds) = nullptr;

    /** Get comprehensive player context: DB time, recent interactions, relationships. */
    inline std::string (*GetPlayerContext)(float withinGameHours) = nullptr;

    /** Get NPC-to-NPC event pair counts within a candidate pool. */
    inline std::string (*GetEventPairCounts)(const char* formIdListCSV, int minSharedEvents) = nullptr;

    // ---- Plugin Configuration API ----

    /** Get the full JSON config for a registered plugin. */
    inline std::string (*GetPluginConfig)(const char* pluginName) = nullptr;

    /** Get a single string config value by dot-path from a plugin's settings. */
    inline std::string (*GetPluginConfigValue)(const char* pluginName, const char* path, const char* defaultValue) = nullptr;

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
            logger::warn("SkyrimNet API v3+ required (found v{})", version);
            return false;
        }

        // v3+ function pointers — Decorator/Tag registration
        DecoratorRegister = reinterpret_cast<int(*)(const std::string&, DecoratorCallback)>(
            GetProcAddress(hDLL, "PublicDecoratorRegister"));

        TagRegister = reinterpret_cast<int(*)(const std::string&, EligibilityCallback)>(
            GetProcAddress(hDLL, "PublicTagRegister"));

        DecoratorExists = reinterpret_cast<bool(*)(const std::string&)>(
            GetProcAddress(hDLL, "PublicDecoratorExists"));

        // v3+ function pointers — Bio Template
        GetBioTemplateName = reinterpret_cast<std::string(*)(uint32_t)>(
            GetProcAddress(hDLL, "PublicGetBioTemplateName"));

        // v3+ function pointers — Data API
        GetMemoriesForActor = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
            GetProcAddress(hDLL, "PublicGetMemoriesForActor"));

        GetRecentEvents = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
            GetProcAddress(hDLL, "PublicGetRecentEvents"));

        GetRecentDialogue = reinterpret_cast<std::string(*)(uint32_t, int)>(
            GetProcAddress(hDLL, "PublicGetRecentDialogue"));

        GetLatestDialogueInfo = reinterpret_cast<std::string(*)()>(
            GetProcAddress(hDLL, "PublicGetLatestDialogueInfo"));

        IsMemorySystemReady = reinterpret_cast<bool(*)()>(
            GetProcAddress(hDLL, "PublicIsMemorySystemReady"));

        GetActorEngagement = reinterpret_cast<std::string(*)(int, bool, bool, double, double)>(
            GetProcAddress(hDLL, "PublicGetActorEngagement"));

        GetRelatedActors = reinterpret_cast<std::string(*)(uint32_t, int, double, double)>(
            GetProcAddress(hDLL, "PublicGetRelatedActors"));

        GetPlayerContext = reinterpret_cast<std::string(*)(float)>(
            GetProcAddress(hDLL, "PublicGetPlayerContext"));

        GetEventPairCounts = reinterpret_cast<std::string(*)(const char*, int)>(
            GetProcAddress(hDLL, "PublicGetEventPairCounts"));

        // Plugin config API
        GetPluginConfig = reinterpret_cast<std::string(*)(const char*)>(
            GetProcAddress(hDLL, "PublicGetPluginConfig"));

        GetPluginConfigValue = reinterpret_cast<std::string(*)(const char*, const char*, const char*)>(
            GetProcAddress(hDLL, "PublicGetPluginConfigValue"));

        logger::info("SkyrimNet API initialized: DecoratorRegister={}, TagRegister={}, DecoratorExists={}",
                     DecoratorRegister != nullptr, TagRegister != nullptr, DecoratorExists != nullptr);
        logger::info("SkyrimNet Data API: Memories={}, Events={}, Dialogue={}, LatestDialogue={}, Ready={}, "
                     "ActorEngagement={}, RelatedActors={}, PlayerContext={}, EventPairs={}",
                     GetMemoriesForActor != nullptr, GetRecentEvents != nullptr,
                     GetRecentDialogue != nullptr, GetLatestDialogueInfo != nullptr,
                     IsMemorySystemReady != nullptr, GetActorEngagement != nullptr,
                     GetRelatedActors != nullptr, GetPlayerContext != nullptr,
                     GetEventPairCounts != nullptr);

        return true;
    }

}  // namespace IntelEngine::SkyrimNetAPI
