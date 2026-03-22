#pragma once

/**
 * FactionConfigLoader - YAML Config Parser for factions.yaml
 *
 * Simple line-based parser (no yaml-cpp dependency) that reads faction
 * definitions and default relations from the IntelEngine factions config.
 *
 * Limitations of the line-based parser:
 *   - Only supports inline arrays: ["a", "b"] (not multi-line YAML arrays)
 *   - Keys must not contain colons
 *   - Comments (#) inside quoted strings are preserved, but nested quotes are not
 *   - Indentation must use spaces (not tabs) for section detection
 */

#include "FactionPolitics.h"

#include <string>
#include <vector>

namespace IntelEngine {

    struct FactionConfigLoadResult {
        std::vector<FactionConfig> factions;
        std::vector<DefaultRelation> defaultRelations;
        bool success = false;
    };

    /** Parse factions.yaml at the given path into config structs. */
    FactionConfigLoadResult LoadFactionConfigFromFile(const std::string& path);

}  // namespace IntelEngine
