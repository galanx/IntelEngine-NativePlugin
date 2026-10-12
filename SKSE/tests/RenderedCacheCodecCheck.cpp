// Standalone C++20 check; no game ABI, Inja or test framework.
#include "RenderedCacheCodec.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Pinned PapyrusUtil Data.h space/empty transport, not the installed DLL.
static std::string Transport(std::string value) {
    if (value.empty()) return "\x1B";
    for (char& byte : value) if (byte == ' ') byte = '\a';
    return value;
}

static std::string Restore(std::string value) {
    if (value == "\x1B") return "";
    for (char& byte : value) if (byte == '\a') byte = ' ';
    return value;
}

using Fields = std::map<std::string, std::string>;
using Objects = std::map<unsigned long long, Fields>;

struct Parsed {
    unsigned declared = 0;
    Objects objects;
};

// Consume written records to EOF: SaveStream counts but skips empty maps.
static Parsed Parse(const std::string& bytes) {
    std::istringstream stream(bytes);
    Parsed result;
    Require(static_cast<bool>(stream >> result.declared), "missing object count");
    while (stream >> std::ws && !stream.eof()) {
        unsigned long long object;
        unsigned count;
        Require(static_cast<bool>(stream >> object >> count), "partial object header");
        Require(count > 0 && count <= bytes.size(), "invalid entry count");
        Fields fields;
        for (unsigned i = 0; i < count; ++i) {
            std::string key, value;
            Require(static_cast<bool>(stream >> key >> value), "partial entry");
            Require(fields.emplace(Restore(key), Restore(value)).second, "duplicate key");
        }
        Require(result.objects.emplace(object, std::move(fields)).second, "duplicate object");
        Require(result.objects.size() <= result.declared, "written count above declared count");
    }
    return result;
}

static std::string Serialize(const Objects& objects) {
    std::ostringstream stream;
    stream << objects.size();
    for (const auto& [object, fields] : objects) {
        if (fields.empty()) continue;
        stream << ' ' << object << ' ' << fields.size();
        for (const auto& [key, value] : fields)
            stream << ' ' << Transport(key) << ' ' << Transport(value);
    }
    return stream.str();
}

int main() {
    try {
        using IntelEngine::EncodeRenderedCache;
        const std::array<std::pair<char, const char*>, 8> tokens{{
            {'%', "%25"}, {'\a', "%07"}, {'\t', "%09"}, {'\n', "%0A"},
            {'\v', "%0B"}, {'\f', "%0C"}, {'\r', "%0D"}, {'\x1B', "%1B"}
        }};
        for (const auto& [byte, token] : tokens)
            Require(EncodeRenderedCache(std::string(1, byte)) == token, "wrong escape mapping");
        Require(EncodeRenderedCache("%0A %250A %25 \\n") == "%250A %25250A %2525 \\n",
                "literal percent/backslash protection");
        Require(EncodeRenderedCache("\a\t\n\v\f\r\x1B%") == "%07%09%0A%0B%0C%0D%1B%25",
                "mixed control mapping");
        // Every supported single byte, including UTF-8 bytes; NUL is not text.
        for (unsigned byte = 1; byte <= 255; ++byte) {
            const std::string text(1, static_cast<char>(byte));
            std::string expected = text;
            for (const auto& [control, token] : tokens)
                if (text[0] == control) expected = token;
            Require(EncodeRenderedCache(text) == expected, "supported byte changed");
        }
        Require(EncodeRenderedCache("").empty(), "empty input changed");
        const std::string utf8 = "\xC3\xA9 \xE9\x9B\xAA \xF0\x9F\x8C\x99";
        Require(EncodeRenderedCache(utf8) == utf8, "UTF-8 changed");
        const std::string literals = "  \"quoted\" \\n %0A %250A %25  ";
        Require(EncodeRenderedCache(literals) == "  \"quoted\" \\n %250A %25250A %2525  ",
                "spaces/quotes/literals changed");
        const std::string facts = "## Things I Know\n\n- I helped \"" + utf8 + "\" (just now)\n";
        const std::string history = "### Past Tasks\nWhat I've done:\n- Delivered %0A (yesterday)\n";
        const std::string heard = "## Rumors I've Heard\n\n- Alice told me: snow\tsoon (just now)\n";
        const std::string told = "## Rumors I've Shared\n\n- I told Bob: snow\r\nsoon (just now)\n";
        const std::vector<std::string> values{
            "", " ", literals, utf8, facts, history, heard + told, told + heard,
            "\a\t\n\v\f\r\x1B%", "\x1B", "%250A%0A\\n\n"
        };
        for (const auto& text : values) {
            const auto encoded = EncodeRenderedCache(text);
            Require(Restore(Transport(encoded)) == encoded, "legacy transport changed encoded text");
            Require(Transport(encoded).find_first_of(" \t\n\v\f\r") == std::string::npos,
                    "transport contains token delimiter");
            Objects expected{{1, {{"Intel_FactsRendered", encoded}, {"other field", "unchanged"}}},
                             {2, {{"rne_name", "Alice Snow"}}},
                             {3, {{"rne_name", utf8}}}};
            const auto parsed = Parse(Serialize(expected));
            Require(parsed.declared == 3 && parsed.objects == expected, "STRV fields changed");
            expected.emplace(4, Fields{});
            const auto skipped = Parse(Serialize(expected));
            expected.erase(4);
            Require(skipped.declared == 4 && skipped.objects == expected, "empty-map skip rejected");
        }
        // Full families in one stream, both participant joins, then unrelated names.
        const Objects families{{1, {{"Intel_FactsRendered", EncodeRenderedCache(facts)}}},
            {2, {{"Intel_GossipRendered", EncodeRenderedCache(heard + told)}}},
            {3, {{"Intel_GossipRendered", EncodeRenderedCache(told + heard)}}},
            {4, {{"Intel_TaskHistoryRendered", EncodeRenderedCache(history)}}},
            {5, {{"rne_name", "Unrelated Name"}}}};
        Require(Parse(Serialize(families)).objects == families, "complete families desynchronized");
        for (const auto& invalid : {"1 1 1 key", "1 1 1 key value trailing", "0 1 1 key value"}) {
            bool rejected = false;
            try { (void)Parse(invalid); } catch (const std::runtime_error&) { rejected = true; }
            Require(rejected, "partial/extra records accepted");
        }
        bool rawRejected = false;
        try {
            const Objects raw{{1, {{"Intel_FactsRendered", facts}}}, {2, {{"rne_name", "Unrelated Name"}}}};
            rawRejected = Parse(Serialize(raw)).objects != raw;
        } catch (const std::runtime_error&) { rawRejected = true; }
        Require(rawRejected, "raw multiline regression control did not fail");
        std::cout << "PASS: 255 text bytes, exact mapping/literals/UTF-8/empty transport; "
                     "facts, both gossip joins, task history and unrelated names; "
                     "full grammar, empty-map count gap and malformed/raw controls.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
