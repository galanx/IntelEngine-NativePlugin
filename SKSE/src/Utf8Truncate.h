#pragma once

#include <cstddef>
#include <string>
#include <string_view>

inline std::size_t Utf8PrefixBytes(std::string_view text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) return text.size();
    auto cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return cut;
}

// maxBytes applies to the retained text; suffix is appended as before.
inline std::string TruncateUtf8Bytes(std::string_view text, std::size_t maxBytes,
                                     std::string_view suffix) {
    if (text.size() <= maxBytes) return std::string(text);
    const auto cut = Utf8PrefixBytes(text, maxBytes);
    std::string result;
    result.reserve(cut + suffix.size());
    result.append(text.data(), cut);
    result.append(suffix);
    return result;
}
