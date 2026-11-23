#include "FactionLeaderNameMatch.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::string Lower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

// Reproduces the old two-way substring condition for regression coverage.
bool OldSubstringMatch(std::string_view displayName, std::string_view configuredName) {
    const auto display = Lower(displayName);
    const auto configured = Lower(configuredName);
    return display.find(configured) != std::string::npos ||
           configured.find(display) != std::string::npos;
}

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main() {
    using IntelEngine::IsFactionLeaderNameMatch;

    Require(OldSubstringMatch("", "Ulfric Stormcloak"), "baseline must reproduce empty-display match");
    Require(OldSubstringMatch("Ulfric Stormcloak", "Jarl Ulfric Stormcloak"),
        "baseline must reproduce reverse substring match");

    Require(!IsFactionLeaderNameMatch("", "Ulfric Stormcloak"), "empty display name matched");
    Require(!IsFactionLeaderNameMatch("Ulfric Stormcloak", ""), "empty configured name matched");
    Require(!IsFactionLeaderNameMatch("Ulfric Stormcloak", "Jarl Ulfric Stormcloak"),
        "reverse match was accepted");
    Require(IsFactionLeaderNameMatch("Jarl Ulfric Stormcloak", "Ulfric Stormcloak"),
        "title-prefixed leader name did not match");
    Require(IsFactionLeaderNameMatch("jArL uLfRiC sToRmClOaK", "ulfric stormcloak"),
        "case-insensitive match failed");
    Require(!IsFactionLeaderNameMatch("Rikket", "Rikke"), "name superstring matched");
    Require(!IsFactionLeaderNameMatch("Rikke\xC3\xA9", "Rikke"), "UTF-8 name suffix matched");
    Require(!IsFactionLeaderNameMatch("\xC3\xA9Rikke", "Rikke"), "UTF-8 name prefix matched");
    Require(IsFactionLeaderNameMatch("Legate Rikke", "Rikke"), "word-boundary leader match failed");
    // any alphabet, any case, with marks or without (UTF-8 written as escapes: this file is ASCII)
    Require(IsFactionLeaderNameMatch("\xD0\xA3\xD0\xBB\xD1\x8C\xD1\x84\xD1\x80\xD0\xB8\xD0\xBA \xD0\x91\xD1\x83\xD1\x80\xD0\xB5\xD0\xB2\xD0\xB5\xD1\x81\xD1\x82\xD0\xBD\xD0\xB8\xD0\xBA",
                                     "\xD1\x83\xD0\xBB\xD1\x8C\xD1\x84\xD1\x80\xD0\xB8\xD0\xBA"),
            "a Cyrillic leader's name in lower case did not match");
    Require(IsFactionLeaderNameMatch("\xC3\x89lisif la Belle", "Elisif"), "a name without its accent did not match");
    Require(IsFactionLeaderNameMatch("Galmar Stein-Faust", "galmar stein-faust") && !IsFactionLeaderNameMatch("Galmar", "Galmar Stein-Faust"),
            "a translated name as configured, and never the reverse");

    std::cout << "FactionLeaderNameMatchCheck: PASS\n";
}
