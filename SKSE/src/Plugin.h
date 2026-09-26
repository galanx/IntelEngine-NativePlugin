#pragma once

/**
 * IntelEngine SKSE Plugin Header
 *
 * Common includes and version information.
 */

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#define INTELENGINE_VERSION "3.5.1"

using namespace std::literals;

namespace logger = SKSE::log;

namespace IntelEngine {
    /** Get the unique save ID for the current playthrough.
     *  Generated on new game, persisted via SKSE serialization. */
    std::string GetSaveUniqueID();

    /** Shared quest handle resolution for IntelEngine's main quest.
     *  Used by Maintenance bootstrap, FixupScriptProperties, and SyncArraysFromSlotTracker. */
    struct QuestHandleResult {
        RE::TESQuest* quest = nullptr;
        RE::BSScript::Internal::VirtualMachine* vm = nullptr;
        RE::VMHandle handle = 0;
        bool valid = false;
    };
    QuestHandleResult ResolveQuestHandle(bool startIfStopped = false);
}
