// Standalone C++20 check; no game ABI or test framework.
#include "NamedPlaceMatch.h"

#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace IntelEngine;
    try {
        const auto places = ParseNamedPlaces(R"([
            {"name":"Kitchen","space":4096,"x":1,"y":2,"z":3,"radius":200},
            {"name":"Kitchen","space":8192,"x":10,"y":20,"z":30},
            {"name":"The Old Well","space":60,"x":0,"y":0,"z":0,"radius":0},
            {"name":"No Space","x":0,"y":0,"z":0},
            {"name":"Zero Space","space":0},
            {"name":"","space":4096},
            {"space":4096},
            "not an object"
        ])");
        Require(places.size() == 3, "entries with no space or no name must be skipped");
        Require(places[1].radius == 0.0f, "missing radius must be 0");

        const auto* kitchen = MatchNamedPlace(places, "the kitchen", 0);
        Require(kitchen && kitchen->space == 4096, "'the kitchen' must match 'Kitchen', first when no space matches");
        Require(MatchNamedPlace(places, "  KITCHEN \t", 0) == kitchen, "case and whitespace must not matter");
        const auto* here = MatchNamedPlace(places, "Kitchen", 8192);
        Require(here && here->space == 8192 && here->x == 10.0f, "same-space match must be preferred");
        Require(MatchNamedPlace(places, "old well", 0) != nullptr, "'the ' must be stripped from the place name too");
        Require(MatchNamedPlace(places, "kitch", 0) == nullptr, "no fuzzy matching");
        Require(MatchNamedPlace(places, "Cellar", 4096) == nullptr, "no match must be nullptr");
        Require(MatchNamedPlace(places, "", 4096) == nullptr && MatchNamedPlace(places, " the ", 4096) == nullptr,
                "empty destination must be nullptr");

        Require(ParseNamedPlaces("{not json").empty(), "malformed JSON must give an empty vector");
        Require(ParseNamedPlaces("").empty() && ParseNamedPlaces(R"({"name":"Kitchen","space":1})").empty(),
                "non-array JSON must give an empty vector");
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
    std::cout << "NamedPlaceMatchCheck: OK\n";
    return 0;
}
