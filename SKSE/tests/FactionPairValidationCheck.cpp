#include "FactionPairValidation.h"

#include <iostream>
#include <stdexcept>
#include <string_view>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using IntelEngine::IsSameFactionPair;
    Require(IsSameFactionPair("Thalmor", "Thalmor"), "identical faction IDs must be rejected");
    Require(!IsSameFactionPair("Thalmor", "Stormcloaks"), "distinct factions must remain valid");
    Require(!IsSameFactionPair("Thalmor", ""), "empty faction B is a valid one-sided event");
    Require(!IsSameFactionPair("", ""), "empty sides are not a same-faction pair");
    std::cout << "PASS: identical faction rejection and valid distinct/one-sided pairs\n";
}
