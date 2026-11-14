#pragma once

/**
 * SoldierTemplates — what a faction's `soldier_template` names.
 *
 * The stock templates are leveled lists of Skyrim.esm ("LCharSoldierImperial"). The game itself keeps no editor
 * ids for leveled lists: TESForm::LookupByEditorID finds such a name only where an editor-id cache fills the
 * game's table for that record type. po3 Tweaks does, and with it the names are found (seen 2026-11-14 on Skyrim
 * 1.5.97: "LCharSoldierImperial" by editor id). A setup without such a cache was not tried; there the lookup by
 * name finds nothing, and the fallback used to be looked up by a name as well. So that a battle does not depend
 * on the cache, the stock names are also known here by form id, and a template may be written as a form id:
 * "0x0001FC5B" (Skyrim.esm) or "MyMod.esp|0x000801".
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

    /** LCharBanditMeleeAny: what a faction without a usable template is given. */
    inline constexpr std::uint32_t kFallbackFormId = 0x0003DECD;

    inline std::string Lower(std::string text) {
        for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    }

    inline std::string Trim(const std::string& text) {
        const auto first = text.find_first_not_of(" \t\r\n\"'");
        if (first == std::string::npos) return {};
        return text.substr(first, text.find_last_not_of(" \t\r\n\"'") - first + 1);
    }

    /** The form id in Skyrim.esm of a stock template name; 0 for any other name. Case does not matter. */
    inline std::uint32_t StockFormId(const std::string& name) {
        struct Stock { const char* name; std::uint32_t formId; };
        static constexpr Stock kStock[] = {
            {"lcharsoldierimperial", 0x0001FC5B}, {"lcharsoldiersons", 0x0001FC5C},
            {"lcharthalmormelee1h", 0x0002B129}, {"lcharbanditmeleeany", 0x0003DECD},
            {"lcharbanditwizard", 0x0001E771},   {"lcharforswornmelee1h", 0x0001E792},
            {"wethiefsubchar", 0x00104DC9},      {"weassassinsubchar", 0x001051F4},
        };
        const std::string wanted = Lower(Trim(name));
        for (const auto& stock : kStock) {
            if (wanted == stock.name) return stock.formId;
        }
        return 0;
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
