#include "FormIDString.h"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using IntelEngine::ParseFormID;
    const auto check = [](std::uint32_t id) {
        const auto parsed = ParseFormID(std::to_string(id));
        return parsed && static_cast<std::uint32_t>(*parsed) == id;
    };
    Require(static_cast<std::uint32_t>(static_cast<float>(0x4E013BF1u)) != 0x4E013BF1u,
            "legacy float transport unexpectedly preserved FormID");
    Require(static_cast<std::uint32_t>(static_cast<float>(0xFE000801u)) != 0xFE000801u,
            "legacy FE float transport unexpectedly preserved FormID");
    Require(ParseFormID("1308703729") == 0x4E013BF1, "decimal 4E013BF1");
    Require(ParseFormID("0x4E013BF1") == 0x4E013BF1, "hexadecimal 4E013BF1");
    Require(check(0xFE000801u) && ParseFormID("0xFE000801") == static_cast<std::int32_t>(0xFE000801u),
            "decimal/hex FE000801 bit round trip");
    Require(check(0xFF000001u) && ParseFormID("0xFF000001") == static_cast<std::int32_t>(0xFF000001u),
            "decimal/hex FF000001 bit round trip");
    for (const auto invalid : {"", "-1", "0x", "12tail", "4294967296", "0x100000000"}) {
        Require(!ParseFormID(invalid), "malformed or overflowing FormID accepted");
    }
    std::cout << "PASS: decimal/hex exact FormIDs, FE/FF signed-bit round trips, float-loss reproductions, invalid/overflow rejection\n";
}
