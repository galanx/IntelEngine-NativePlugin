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

    std::cout << "FactionLeaderNameMatchCheck: PASS\n";
}
