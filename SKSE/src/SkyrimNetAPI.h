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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h defines macros that conflict with CommonLibSSE
#ifdef GetObject
#undef GetObject
#endif
#ifdef SendMessage
#undef SendMessage
#endif

namespace RE { class Actor; }

namespace IntelEngine::SkyrimNetAPI {

    // ---- Core ----
    inline int (*GetVersion)() = nullptr;

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

    // ---- Decorator API (v3.1+) ----

    /** Register a native C++ decorator. Callback receives RE::Actor*, returns string for Inja templates.
     *  Used in eligibilityRules (synchronous, no cache delay) and prompt templates. */
    inline bool (*RegisterDecorator)(const char* name, const char* description,
        std::function<std::string(RE::Actor*)> callback) = nullptr;

    /** Check if a decorator with this name already exists. */
    inline bool (*HasDecorator)(const char* name) = nullptr;

    // ---- Actor Busy API (v3.1+) ----

    /** Mark an actor as busy with a reason string. Blocks actions with is_busy eligibility check. */
    inline bool (*SetActorBusy)(uint32_t formId, const char* reason) = nullptr;

    /** Clear busy state for an actor. */
    inline bool (*ClearActorBusy)(uint32_t formId) = nullptr;

    /** Check if an actor is busy. */
    inline bool (*IsActorBusy)(uint32_t formId) = nullptr;

    // ---- Event Callback API (v3.1+) ----

    /** Register a callback for a specific event type (e.g., "dialogue"). Thread-safe. */
    inline uint64_t (*RegisterEventCallback)(const char* eventType, std::function<void(const char*)> callback) = nullptr;

    /** Unregister a previously registered event callback by ID. */
    inline bool (*UnregisterEventCallback)(uint64_t callbackId) = nullptr;

    // ---- World Knowledge API (v7+ for global, v9+ for per-actor) ----

    /** Get all world knowledge entries as a JSON array.
     *  Each entry includes id, content, condition_expr, always_inject, importance,
     *  display_name, is_active. Returns "[]" on error. v7+. */
    inline std::string (*GetWorldKnowledge)(int maxCount) = nullptr;

    /** Get world knowledge entries applicable to an actor as a JSON array.
     *  Empty searchQuery returns deterministic always-inject entries only (cheap, no HNSW).
     *  Non-empty searchQuery enables semantic search. Returns "[]" on error or if SkyrimNet older than v9. */
    inline std::string (*GetWorldKnowledgeForActor)(uint32_t formId, int maxResults, const char* searchQuery) = nullptr;

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

        // v4+ function pointers — Decorator registration

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

        // Decorator API (v3.1+)
        RegisterDecorator = reinterpret_cast<bool(*)(const char*, const char*,
            std::function<std::string(RE::Actor*)>)>(
            GetProcAddress(hDLL, "PublicRegisterDecorator"));

        HasDecorator = reinterpret_cast<bool(*)(const char*)>(
            GetProcAddress(hDLL, "PublicHasDecorator"));

        // Actor Busy API (v3.1+)
        SetActorBusy = reinterpret_cast<bool(*)(uint32_t, const char*)>(
            GetProcAddress(hDLL, "PublicSetActorBusy"));

        ClearActorBusy = reinterpret_cast<bool(*)(uint32_t)>(
            GetProcAddress(hDLL, "PublicClearActorBusy"));

        IsActorBusy = reinterpret_cast<bool(*)(uint32_t)>(
            GetProcAddress(hDLL, "PublicIsActorBusy"));

        // Event callback API (v3.1+)
        RegisterEventCallback = reinterpret_cast<uint64_t(*)(const char*, std::function<void(const char*)>)>(
            GetProcAddress(hDLL, "PublicRegisterEventCallback"));

        UnregisterEventCallback = reinterpret_cast<bool(*)(uint64_t)>(
            GetProcAddress(hDLL, "PublicUnregisterEventCallback"));

        // World knowledge API — global (v7+) and per-actor (v9+).
        // Null on older SkyrimNet builds; callers must null-check.
        GetWorldKnowledge = reinterpret_cast<std::string(*)(int)>(
            GetProcAddress(hDLL, "PublicGetWorldKnowledge"));
        GetWorldKnowledgeForActor = reinterpret_cast<std::string(*)(uint32_t, int, const char*)>(
            GetProcAddress(hDLL, "PublicGetWorldKnowledgeForActor"));

        // Bio template API
        logger::info("SkyrimNet Data API: Memories={}, Events={}, Dialogue={}, LatestDialogue={}, Ready={}, "
                     "ActorEngagement={}, RelatedActors={}, PlayerContext={}, EventPairs={}, "
                     "EventCallback={}, BioTemplate={}, WorldKnowledge={}",
                     GetMemoriesForActor != nullptr, GetRecentEvents != nullptr,
                     GetRecentDialogue != nullptr, GetLatestDialogueInfo != nullptr,
                     IsMemorySystemReady != nullptr, GetActorEngagement != nullptr,
                     GetRelatedActors != nullptr, GetPlayerContext != nullptr,
                     GetEventPairCounts != nullptr,
                     RegisterEventCallback != nullptr, GetBioTemplateName != nullptr,
                     GetWorldKnowledgeForActor != nullptr);

        return true;
    }

}  // namespace IntelEngine::SkyrimNetAPI
