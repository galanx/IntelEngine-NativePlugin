#pragma once

/**
 * SoldierTemplates — what a faction's `soldier_template` names.
 *
 * A name ("LCharSoldierImperial") is looked up by Papyrus::LookupLeveledActor, like every other spawn's. Besides
 * a name a template may be written as a form id: "0x0001FC5B" (Skyrim.esm) or "MyMod.esp|0x000801". This header
 * tells the two apart.
 *
 * Pure text handling, no game types: the offline check compiles this header alone.
 */

#include <cctype>
#include <cstdint>
#include <string>

namespace IntelEngine::SoldierTemplates {

    struct Ref {
        std::string plugin;          // empty: the form id is complete as it stands (Skyrim.esm, load index 00)
        std::uint32_t formId = 0;
    };

    /** LCharBanditMeleeAny in Skyrim.esm: the last resort when the bandit fallback's name is not found either. */
    inline constexpr std::uint32_t kFallbackFormId = 0x0003DECD;

    inline std::string Trim(const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r\n\"'");
        if (first == std::string::npos) return {};
        return text.substr(first, text.find_last_not_of(" \t\r\n\"'") - first + 1);
    }

    /** "0x0001FC5B" or "Plugin.esp|0x000801" (also without the 0x): true and the parts. Anything else is a name. */
    inline bool ParseRef(const std::string& text, Ref& out) {
        std::string value = Trim(text);
        std::string plugin;
        const auto bar = value.find('|');
        if (bar != std::string::npos) {
            plugin = Trim(value.substr(0, bar));
            value = Trim(value.substr(bar + 1));
            if (plugin.empty()) return false;
        }
        const bool prefixed = value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
        if (!prefixed && plugin.empty()) return false;          // a bare word is an editor id, even "DEAD"
        const std::string digits = prefixed ? value.substr(2) : value;
        if (digits.empty() || digits.size() > 8) return false;
        std::uint32_t id = 0;
        for (const char c : digits) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
            id = id * 16 + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10);
        }
        if (id == 0) return false;
        out.plugin = plugin;
        out.formId = id;
        return true;
    }

}  // namespace IntelEngine::SoldierTemplates
