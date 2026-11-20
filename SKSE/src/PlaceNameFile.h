#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace IntelEngine {

    // One row of a Data/SKSE/Plugins/IntelEngine/placenames/*.tsv file:
    //   <master>\t<local FormID, hex>\t<L = location, C = interior cell>\t<name, UTF-8>
    // Blank lines and lines starting with '#' are skipped, as is a UTF-8 byte order mark.
    struct PlaceNameRow {
        std::string master;
        std::uint32_t localId = 0;
        char type = 0;
        std::string name;
    };

    inline std::optional<PlaceNameRow> ParsePlaceNameLine(std::string_view line) {
        if (line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
        if (line.empty() || line.front() == '#') return std::nullopt;

        std::string_view fields[4];
        for (int i = 0; i < 3; ++i) {
            const auto tab = line.find('\t');
            if (tab == std::string_view::npos) return std::nullopt;
            fields[i] = line.substr(0, tab);
            line.remove_prefix(tab + 1);
        }
        fields[3] = line;

        PlaceNameRow row;
        auto id = fields[1];
        if (id.starts_with("0x") || id.starts_with("0X")) id.remove_prefix(2);
        const auto [end, ec] = std::from_chars(id.data(), id.data() + id.size(), row.localId, 16);
        if (ec != std::errc{} || end != id.data() + id.size() || id.empty()) return std::nullopt;
        if (fields[2] != "L" && fields[2] != "C") return std::nullopt;
        if (fields[0].empty() || fields[3].empty()) return std::nullopt;
        row.master = fields[0];
        row.type = fields[2].front();
        row.name = fields[3];
        return row;
    }
}
