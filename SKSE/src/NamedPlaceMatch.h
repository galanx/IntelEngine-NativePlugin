#pragma once

// Pure matching logic for SkyrimNet Named Places (PublicListPlaces JSON). No game ABI, so
// tests/NamedPlaceMatchCheck.cpp can build it standalone.

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace IntelEngine {

    struct NamedPlace {
        std::string name;
        uint32_t space;  // interior cell FormID, or worldspace FormID outdoors
        float x, y, z, radius;
    };

    // Entries without a non-empty string name or a non-zero space are skipped; malformed JSON gives none.
    inline std::vector<NamedPlace> ParseNamedPlaces(const std::string& json) {
        std::vector<NamedPlace> places;
        const auto parsed = nlohmann::json::parse(json, nullptr, false);
        if (!parsed.is_array()) return places;
        for (const auto& entry : parsed) {
            if (!entry.is_object()) continue;
            const auto name = entry.find("name");
            const auto space = entry.find("space");
            if (name == entry.end() || !name->is_string() || name->get_ref<const std::string&>().empty()) continue;
            if (space == entry.end() || !space->is_number_unsigned() || space->get<uint64_t>() == 0) continue;
            const auto num = [&](const char* key) {
                const auto it = entry.find(key);
                return it != entry.end() && it->is_number() ? it->get<float>() : 0.0f;
            };
            places.push_back({name->get<std::string>(), static_cast<uint32_t>(space->get<uint64_t>()),
                              num("x"), num("y"), num("z"), num("radius")});
        }
        return places;
    }

    // Trim, ASCII-lowercase, strip one leading "the ".
    inline std::string NormalizePlaceName(std::string name) {
        const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        while (!name.empty() && isSpace(name.back())) name.pop_back();
        size_t start = 0;
        while (start < name.size() && isSpace(name[start])) ++start;
        name.erase(0, start);
        for (auto& c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (name.rfind("the ", 0) == 0) name.erase(0, 4);
        return name;
    }

    // Exact match of normalized names. Among matches a place in currentSpace wins, else the first.
    inline const NamedPlace* MatchNamedPlace(const std::vector<NamedPlace>& places, const std::string& destination,
                                             uint32_t currentSpace) {
        const auto wanted = NormalizePlaceName(destination);
        if (wanted.empty()) return nullptr;
        const NamedPlace* first = nullptr;
        for (const auto& place : places) {
            if (NormalizePlaceName(place.name) != wanted) continue;
            if (place.space == currentSpace) return &place;
            if (!first) first = &place;
        }
        return first;
    }

}  // namespace IntelEngine
