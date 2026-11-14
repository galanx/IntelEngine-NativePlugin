#pragma once

/**
 * JsonKeys — find a JSON object's member by a name that came from a script.
 *
 * The game keeps one spelling per string, whatever its case: the first one any loaded script used. A script that
 * writes "formIds" hands the DLL "formIDs" when another script (one of ours: IntelEngine_StoryEngine has a
 * variable of that name) was loaded first. Matching the key exactly then finds nothing. Seen 2026-11-14:
 * GetJsonArrayInt(json, "formIds", i) answered 0 for every soldier of every battle, so the battle script never
 * got hold of a single one of them ("Kick-started 0/6 combat pairs").
 *
 * Every member name a script asks for is therefore matched without regard to case; the exact spelling wins when
 * both exist. No game types: the offline check compiles this header alone.
 */

#include <cctype>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace IntelEngine::JsonKeys {

    inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }

    /** The member of `object` named `key`, exact spelling first, then any case; nullptr when there is none
     *  (or `object` is not an object). */
    inline const nlohmann::json* Find(const nlohmann::json& object, const std::string& key) {
        if (!object.is_object()) return nullptr;
        if (const auto exact = object.find(key); exact != object.end()) return &*exact;
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (EqualsIgnoreCase(it.key(), key)) return &*it;
        }
        return nullptr;
    }

    /** Element `index` of the array member `key` as a form id for a script: ids of created references are above
     *  INT_MAX (they start with FF), so the number is read unsigned and handed over as the same 32 bits.
     *  0 when the member is missing, not an array, too short, or the element is not such a number. */
    inline int ArrayInt(const nlohmann::json& object, const std::string& key, int index) {
        const auto* array = Find(object, key);
        if (!array || !array->is_array() || index < 0 || static_cast<std::size_t>(index) >= array->size()) return 0;
        const auto& element = (*array)[static_cast<std::size_t>(index)];
        if (element.is_number_unsigned()) return static_cast<int>(element.get<std::uint32_t>());
        if (element.is_number_integer()) return static_cast<int>(element.get<std::int64_t>());
        return 0;
    }

}  // namespace IntelEngine::JsonKeys
