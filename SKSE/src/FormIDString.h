#pragma once

#include <bit>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace IntelEngine {
    inline std::optional<std::int32_t> ParseFormID(std::string_view text) {
        if (text.empty()) return std::nullopt;

        int base = 10;
        if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            text.remove_prefix(2);
            if (text.empty()) return std::nullopt;
        }

        std::uint32_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
        if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
        return std::bit_cast<std::int32_t>(value);
    }
}
