#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <nlohmann/json.hpp>

namespace IntelEngine {
    // PE API-GUIDE section 3: caller-owned buffer, at most four attempts.
    template<class Reader>
    std::optional<nlohmann::json> ReadJSON(Reader read) {
        std::vector<char> bytes;
        for (int attempt = 0; attempt < 4; ++attempt) {
            auto required = read(bytes.empty() ? nullptr : bytes.data(),
                                 static_cast<std::uint32_t>(bytes.size()));
            if (!required || required > 64u * 1024u * 1024u) return std::nullopt;
            if (required > bytes.size()) {
                bytes.resize(required);
                continue;
            }
            if (bytes[required - 1] != '\0') return std::nullopt;
            auto value = nlohmann::json::parse(bytes.data(), bytes.data() + required - 1, nullptr, false);
            if (value.is_discarded()) return std::nullopt;
            return value;
        }
        return std::nullopt;
    }
}
