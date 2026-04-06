#pragma once

/**
 * IntelEngine SKSE Plugin Header
 *
 * Common includes and version information.
 */

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#define INTELENGINE_VERSION "3.2.2"

using namespace std::literals;

namespace logger = SKSE::log;

namespace IntelEngine {
    /** Get the unique save ID for the current playthrough.
     *  Generated on new game, persisted via SKSE serialization. */
    std::string GetSaveUniqueID();
}
