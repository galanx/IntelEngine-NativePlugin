#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
namespace RE {
struct StaticFunctionTag {};
struct BSFixedString {
    std::string value;
    BSFixedString(const char* text) : value(text) {}
    const char* c_str() const { return value.c_str(); }
};
}
namespace logger {
inline int warnings = 0, debugs = 0;
template<class... T> void info(T&&...) {}
template<class... T> void warn(T&&...) { ++warnings; }
template<class... T> void debug(T&&...) { ++debugs; }
}
#include "StoryResponseProduction.inc"
static void Require(bool value) { if (!value) throw std::runtime_error("field lookup mismatch"); }
int main() {
    Require(std::string(StoryResponseGetOptionalField(nullptr, "{\"Value\":\"yes\"}", "value").c_str()) == "yes");
    Require(std::string(StoryResponseGetOptionalField(nullptr, "{}", "meetLocation").c_str()).empty());
    Require(logger::warnings == 0 && logger::debugs == 1);
    Require(std::string(StoryResponseGetField(nullptr, "{}", "required").c_str()).empty());
    Require(logger::warnings == 1 && logger::debugs == 1);
    Require(std::string(StoryResponseGetOptionalField(nullptr, "{\"meetLocation\":null}", "meetLocation").c_str()).empty());
    Require(logger::warnings == 1 && logger::debugs == 1);
    StoryResponseGetOptionalField(nullptr, "invalid JSON", "meetLocation");
    Require(logger::warnings == 2);
    StoryResponseGetField(nullptr, "{\"required\":false}", "required");
    Require(logger::warnings == 2);
    std::cout << "PASS: optional misses debug, required misses warn, malformed JSON warns, case/null/cache values\n";
}
