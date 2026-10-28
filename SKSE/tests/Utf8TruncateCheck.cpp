#include "Utf8Truncate.h"

#include <stdexcept>
#include <string>

int main() {
    const std::string emoji = "\xF0\x9F\x98\x80";
    const auto require = [](bool condition) {
        if (!condition) throw std::runtime_error("UTF-8 truncation check failed");
    };

    require(TruncateUtf8Bytes(std::string(79, 'a') + emoji + "tail", 80, "...") ==
            std::string(79, 'a') + "...");
    require(TruncateUtf8Bytes(std::string(76, 'a') + emoji + "tail", 80, "...") ==
            std::string(76, 'a') + emoji + "...");
    const std::string twoByte = "\xC3\xA9";
    const std::string threeByte = "\xE2\x82\xAC";
    require(TruncateUtf8Bytes(std::string(79, 'a') + twoByte, 80, "...") ==
            std::string(79, 'a') + "...");
    require(TruncateUtf8Bytes(std::string(78, 'a') + threeByte, 80, "...") ==
            std::string(78, 'a') + "...");
    require(TruncateUtf8Bytes(twoByte, 1, "...") == "...");
    require(TruncateUtf8Bytes(threeByte, 2, "...") == "...");
    require(TruncateUtf8Bytes(threeByte, 0, "...") == "...");
    require(TruncateUtf8Bytes(std::string(80, 'a') + "tail", 80, "...") ==
            std::string(80, 'a') + "...");
    require(TruncateUtf8Bytes("short", 80, "...") == "short");
}
