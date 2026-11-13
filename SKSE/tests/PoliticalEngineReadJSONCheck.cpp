// Standalone production-reader check; no game ABI or test framework.
#include "PoliticalEngineReadJSON.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        using IntelEngine::ReadJSON;
        const std::string payload = R"({"revision":42})";
        const auto required = static_cast<std::uint32_t>(payload.size() + 1);
        auto copy = [&](char* out, std::uint32_t capacity) {
            if (out && capacity >= required) std::memcpy(out, payload.c_str(), required);
            return required;
        };
        {
            int calls = 0;
            auto value = ReadJSON([&](char* out, std::uint32_t capacity) {
                ++calls;
                if (calls == 1) {
                    Require(!out && capacity == 0, "initial size query");
                    return 3u;
                }
                if (calls == 2) Require(out && capacity == 3, "growth size query");
                return copy(out, capacity);
            });
            Require(value && value->value("revision", 0) == 42 && calls == 3, "growth retry result");
            calls = 0;
            Require(!ReadJSON([&](char*, std::uint32_t capacity) {
                ++calls;
                return capacity + 1;
            }) && calls == 4, "growth retry bounded to four attempts");
            std::cout << "PASS growth-retry\n";
        }
        {
            char shortBuffer[] = "XYZ";
            Require(copy(shortBuffer, 3) == required && std::strcmp(shortBuffer, "XYZ") == 0,
                    "short buffer must remain untouched");
            auto value = ReadJSON(copy);
            Require(value && value->value("revision", 0) == 42, "size query followed by complete copy");
            std::cout << "PASS short-buffer-size-query\n";
        }
        {
            auto value = ReadJSON([](char* out, std::uint32_t capacity) {
                if (out && capacity >= 3) std::memcpy(out, "{}!", 3);
                return 3u;
            });
            Require(!value, "missing NUL accepted");
            std::cout << "PASS missing-NUL rejected\n";
        }
        {
            Require(!ReadJSON([](char*, std::uint32_t) { return 0u; }), "zero accepted");
            std::cout << "PASS zero -> nullopt\n";
        }
        {
            int calls = 0;
            Require(!ReadJSON([&](char*, std::uint32_t) {
                ++calls;
                return 64u * 1024u * 1024u + 1;
            }) && calls == 1, "over-cap must stop before allocation/retry");
            std::cout << "PASS over-cap -> nullopt\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
