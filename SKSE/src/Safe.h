#pragma once

// Reads that must not throw or crash on what the plugin did not write itself: a model's answer, another mod's
// records, a name the game returns as nothing. nlohmann's value() throws when a key holds the wrong type ("turn": 2,
// "display_name": null); in a callback on SkyrimNet's thread or a detached thread of our own that ends the game.

#include <string>

#include <nlohmann/json.hpp>

namespace IntelEngine::Safe {

    // The text under `key`, or the fallback when it is missing or is no text.
    inline std::string Text(const nlohmann::json& j, const char* key, std::string fallback = {}) {
        if (!j.is_object()) return fallback;
        const auto it = j.find(key);
        return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
    }

    // The whole number under `key`, or the fallback when it is missing or is no number.
    inline int Int(const nlohmann::json& j, const char* key, int fallback = 0) {
        if (!j.is_object()) return fallback;
        const auto it = j.find(key);
        if (it == j.end()) return fallback;
        if (it->is_number_integer() || it->is_number_unsigned()) return static_cast<int>(it->get<long long>());
        if (it->is_number_float()) return static_cast<int>(it->get<double>());
        return fallback;
    }

    // The number under `key` (whole or not), or the fallback.
    inline double Number(const nlohmann::json& j, const char* key, double fallback = 0.0) {
        if (!j.is_object()) return fallback;
        const auto it = j.find(key);
        return it != j.end() && it->is_number() ? it->get<double>() : fallback;
    }

    // A name of the game as text: the game hands back a null pointer for some forms.
    inline const char* Name(const char* name, const char* fallback = "") { return name ? name : fallback; }
}
