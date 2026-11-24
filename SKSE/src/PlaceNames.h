#pragma once

// 4.0: how a destination names a SkyrimNet place (PlaceTargets.cpp). Pure, checked by PlaceNamesCheck.

#include <cctype>
#include <string>

namespace IntelEngine::PlaceTargets {

    // Lower case, trimmed, single spaces, without a leading "the " and trailing punctuation.
    inline std::string Fold(const std::string& text) {
        std::string out;
        bool space = false;
        for (const unsigned char c : text) {
            if (std::isspace(c)) {
                space = !out.empty();
                continue;
            }
            if (space) out += ' ';
            space = false;
            out += static_cast<char>(std::tolower(c));
        }
        while (!out.empty() && (out.back() == '.' || out.back() == '!' || out.back() == '?' || out.back() == ',')) out.pop_back();
        if (out.starts_with("the ")) out.erase(0, 4);
        return out;
    }

    // Does the destination name this place (its folded name, or with its location's: "kitchen in breezehome")?
    inline bool Names(const std::string& wanted, const std::string& placeName, const std::string& locationName) {
        const auto name = Fold(placeName);
        if (name.empty() || wanted.empty()) return false;
        if (wanted == name) return true;
        const auto where = Fold(locationName);
        if (where.empty()) return false;
        for (const auto& form : {where + " " + name, name + " in " + where, name + " at " + where, name + " of " + where,
                                 where + "'s " + name}) {
            if (wanted == form) return true;
        }
        return false;
    }

}
