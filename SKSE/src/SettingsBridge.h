#pragma once

/**
 * SettingsBridge — IntelEngine's gameplay settings on SkyrimNet's plugin settings page.
 *
 * The settings a player changes in the MCM and on IntelEngine's own dashboard live in the save (StorageUtil
 * values and script properties). SkyrimNet shows a settings page for every plugin from the fields in
 * config/plugins/<Name>/manifest.yaml and keeps the values in config/plugins/<Name>/settings.yaml. This
 * bridge keeps the two in step through one table:
 *
 *  - SyncToFile: the game's values go into the file (after a change in the MCM or on the dashboard, and
 *    once for a save from before the settings were shared).
 *  - ApplyFromFile: the file's values go into the game (after a save on SkyrimNet's page, and on every load,
 *    so a new game starts with what the player set). A value the file lacks is filled in from the game; one
 *    that cannot be read (unknown choice, not a number) is left as the game has it.
 *  - ManifestFields: the schema fields for manifest.yaml, generated from the same table, so the page cannot
 *    offer a setting or a range the bridge does not know.
 *
 * The game side of each setting is the "config" object of the dashboard state (Core.BuildConfigJson) for
 * reading and the existing setting events for writing (IntelEngine_DashboardSetting with the same key, or
 * IntelEngine_DashboardToggleStory for a story type), so no setting is implemented twice.
 * No game types here: the logic is checked offline with an in-memory store.
 */

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace IntelEngine::SettingsBridge {

    enum class Kind { Bool, Int, Float, Select };

    struct Entry {
        const char* key;          // key in the dashboard's config object and of Core.OnDashboardSetting
        const char* section;      // settings.yaml: top-level section
        const char* name;         // settings.yaml: key inside the section
        Kind kind;
        double min;               // Int / Float range
        double max;
        const char* options;      // Select: the choices, '|' between them; the stored number is the index
        double def;               // default (Select: index), as the scripts have it
        const char* category;     // heading on SkyrimNet's page
        const char* label;
        const char* description;
        const char* event;        // nullptr: IntelEngine_DashboardSetting with `key`; else this event ...
        const char* eventArg;     // ... with this string argument
        // The file and the page hold the opposite of the game's value (Bool only). The game stores "skip this
        // action for followers"; a page of such switches, all off, reads as "switched off for followers".
        // The page shows "followers can do this", on by default. `def` is the file's side.
        bool inverted = false;
    };

    /** Reads and writes one value of settings.yaml. `read` answers false when the key is not there. */
    struct Store {
        std::function<bool(const std::string& section, const std::string& name, std::string& value)> read;
        std::function<bool(const std::string& section, const std::string& name, const std::string& yamlValue)> write;
    };

    /** One setting event to send to the scripts. */
    struct Change {
        std::string event;
        std::string arg;
        float value = 0.0f;
    };

    const std::vector<Entry>& Table();

    /**
     * The settings object as the scripts build it (Core.BuildConfigJson), parsed. Script strings are
     * case-insensitive in the game: a script's "true" arrives as "TRUE" or "True" when any other script used
     * that spelling first, which is not JSON, and a key can change case the same way. So the words true, false
     * and null are read in any case, and the keys of the result are in lower case (the table's keys are looked
     * up that way). Returns null (not an object) when the text is not a JSON object even so.
     */
    nlohmann::json ParseConfig(const std::string& scriptJson);

    /** The game wins: every table value present in `config` that differs from the file is written. Returns the count. */
    int SyncToFile(const nlohmann::json& config, const Store& store);

    /** The file wins: the events that bring the game to the file's values. Keys the file lacks are written from `config`. */
    std::vector<Change> ApplyFromFile(const nlohmann::json& config, const Store& store);

    /** The `schema.fields` entries for manifest.yaml (indented as the file is), ending with a newline. */
    std::string ManifestFields();

    // Exposed for the offline check.
    std::vector<std::string> OptionsOf(const Entry& entry);
    std::string ToYaml(const Entry& entry, double value);
    bool FromYaml(const Entry& entry, const std::string& text, double& value);

}  // namespace IntelEngine::SettingsBridge
