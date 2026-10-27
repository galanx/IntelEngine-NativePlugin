#pragma once

#include <string>
#include <string_view>

namespace IntelEngine {

// Papyrus text only (no embedded NUL). Encode once at the complete cache write.
inline std::string EncodeRenderedCache(std::string_view text) {
    std::string encoded;
    encoded.reserve(text.size() <= encoded.max_size() / 3 ? text.size() * 3 : text.size());
    for (const char byte : text) {
        switch (byte) {
        case '%':  encoded += "%25"; break;
        case '\a': encoded += "%07"; break;
        case '\t': encoded += "%09"; break;
        case '\n': encoded += "%0A"; break;
        case '\v': encoded += "%0B"; break;
        case '\f': encoded += "%0C"; break;
        case '\r': encoded += "%0D"; break;
        case '\x1B': encoded += "%1B"; break;
        default: encoded += byte; break;
        }
    }
    return encoded;
}

}  // namespace IntelEngine
