#include "PlaceNameFile.h"
#include "Utf8Fold.h"

#include <iostream>
#include <stdexcept>
#include <string>

// UTF-8 bytes are written as escapes so the check does not depend on the compiler's source charset.
static const std::string kWinterholdRu = "\xD0\x92\xD0\xB8\xD0\xBD\xD1\x82\xD0\xB5\xD1\x80\xD1\x85\xD0\xBE\xD0\xBB\xD0\xB4";       // Винтерхолд
static const std::string kWinterholdRuLower = "\xD0\xB2\xD0\xB8\xD0\xBD\xD1\x82\xD0\xB5\xD1\x80\xD1\x85\xD0\xBE\xD0\xBB\xD0\xB4";  // винтерхолд
static const std::string kWinterholdRuTypo =
    "\xD0\xB2\xD0\xB8\xD0\xBD\xD1\x82\xD0\xB5\xD1\x80\xD1\x85\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB4";  // винтерхольд

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using IntelEngine::FoldUtf8;
    using IntelEngine::Utf8Levenshtein;

    Require(FoldUtf8(kWinterholdRu) == kWinterholdRuLower, "Cyrillic lowercase");
    Require(FoldUtf8("\xC3\x84LTERE") == "\xC3\xA4ltere", "accented Latin lowercase");  // ÄLTERE -> ältere
    Require(FoldUtf8("Dragonsreach") == "dragonsreach", "ASCII lowercase unchanged");
    Require(FoldUtf8("") == "", "empty string");
    Require(FoldUtf8("AB\xE4") == "ab\xE4", "invalid UTF-8 keeps bytes, lowers ASCII only");
    Require(FoldUtf8("\xE2\x84\xA6") == "\xE2\x84\xA6", "length-changing fold (ohm sign) is left alone");

    Require(Utf8Levenshtein(kWinterholdRuLower, kWinterholdRuTypo) == 1, "one Cyrillic letter costs 1");
    Require(Utf8Levenshtein(kWinterholdRu, kWinterholdRuLower) == 0, "Cyrillic distance ignores case");
    Require(Utf8Levenshtein("kitten", "sitting") == 3, "ASCII distance");
    Require(Utf8Levenshtein("Kitten", "kitten") == 0, "ASCII distance ignores case");
    Require(Utf8Levenshtein("", "abc") == 3, "empty input");
    Require(Utf8Levenshtein("a\xE4", "ab") == 1, "invalid UTF-8 falls back to bytes");

    using IntelEngine::ParsePlaceNameLine;
    const auto row = ParsePlaceNameLine("\xEF\xBB\xBFSkyrim.esm\t0x018A56\tL\tWhiterun\r\n");
    Require(row && row->master == "Skyrim.esm" && row->localId == 0x018A56 && row->type == 'L' && row->name == "Whiterun",
            "place-name row with BOM, 0x prefix and CRLF");
    const auto bare = ParsePlaceNameLine("Dawnguard.esm\t357e\tC\tCastle Volkihar\textra");
    Require(bare && bare->localId == 0x357E && bare->type == 'C' && bare->name == "Castle Volkihar\textra",
            "hex without prefix; a tab inside the name is kept");
    const auto german = ParsePlaceNameLine("Skyrim.esm\t18A56\tL\tWei\xC3\x9F" "lauf");  // Weißlauf
    Require(german && german->name == "Wei\xC3\x9F" "lauf", "UTF-8 name kept as bytes");
    Require(!ParsePlaceNameLine("# comment") && !ParsePlaceNameLine("") && !ParsePlaceNameLine("\r"), "comment and blank lines");
    Require(!ParsePlaceNameLine("Skyrim.esm\t18A56\tX\tWhiterun"), "unknown type");
    Require(!ParsePlaceNameLine("Skyrim.esm\t18G56\tL\tWhiterun"), "bad hex");
    Require(!ParsePlaceNameLine("Skyrim.esm\t18A56\tL"), "missing name");
    Require(!ParsePlaceNameLine("Skyrim.esm\t18A56\tL\t"), "empty name");

    std::cout << "PASS: place-name rows; Cyrillic/accented folding, ASCII parity, invalid-UTF-8 and length-guard fallbacks, "
                 "character-based Levenshtein\n";
}
