// Standalone C++20; no game ABI. From a VS developer shell, add the existing
// vcpkg include directory: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src /I <vcpkg-include> SKSE/tests/BioContentCheck.cpp
#include "BioContent.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace BC = IntelEngine::BioContent;
namespace fs = std::filesystem;
using nlohmann::json;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static void Put(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << content;
    Require(static_cast<bool>(file), "fixture write failed");
}

static std::string Text(const std::string& label) {
    return "{% block summary %}\r\n " + label + " \r\n{% endblock %}\n"
           "{% block relationships %}\n Ally of " + label + ". \n{% endblock %}";
}

int main() {
    const auto root = fs::temp_directory_path() / ("ie-bio-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{root};
    const std::string name = "test_actor_ABC";
    const auto logical = "prompts/characters/" + name + ".prompt";
    const auto dynamic = "prompts/characters/dynamic/" + name + ".dynamic.prompt";
    const auto base = root / "library/skyrimnet.base";
    const auto pack = root / "library/test.pack";
    const auto external = root / "external/test.external";
    json registry = {
        {"priority", {"test.external", "test.pack"}},
        {"plugins", {{"test.pack", {{"enabled", true}, {"source", "hub"}}},
                     {"test.external", {{"enabled", true}, {"source", "external"}}}}},
        {"pins", json::object()}, {"items", json::object()}};
    const auto saveRegistry = [&] { Put(root / "content-registry.json", registry.dump()); };
    const auto manifest = [&](const fs::path& path, const char* id, const char* version) {
        Put(path / "manifest.json", json{{"id", id}, {"version", version}}.dump());
    };
    const auto expect = [&](const std::string& label, const std::string& save = "current") {
        auto bio = BC::Resolve(root, name, save);
        Require(bio.has_value(), "expected resolved bio");
        Require(BC::ExtractBlock(bio->content, "summary") == label, ("wrong summary: " + label).c_str());
        Require(BC::ExtractBlock(bio->content, "relationships") == "Ally of " + label + ".", "wrong relationships");
    };
    saveRegistry();
    manifest(base, "skyrimnet.base", "1.0.0");
    Put(root / "prompts/characters" / (name + ".prompt"), Text("legacy"));
    expect("legacy");
    Put(base / logical, Text("base"));
    expect("base");
    auto trailingRoot = BC::Resolve(root / "", name, "current");
    Require(trailingRoot && BC::ExtractBlock(trailingRoot->content, "summary") == "base", "trailing root separator changed resolution");
    manifest(pack, "test.pack", "1.0.0");
    Put(pack / logical, Text("package"));
    expect("package");
    manifest(external, "test.external", "1.0.0");
    Put(external / logical, Text("external"));
    expect("external");
    registry["priority"] = {"test.pack", "test.external"}; saveRegistry();
    expect("package");
    registry["plugins"]["test.pack"]["enabled"] = false; saveRegistry();
    expect("external");
    registry["plugins"]["test.pack"]["enabled"] = true;
    registry["plugins"]["test.pack"]["missing"] = true; saveRegistry();
    expect("external");
    registry["plugins"].erase("test.pack"); saveRegistry();
    expect("external");
    Put(root / "overlay" / logical, Text("overlay"));
    expect("overlay");
    registry["pins"][BC::Fold(logical)] = "skyrimnet.base"; saveRegistry();
    expect("base");
    Put(root / "saves/current" / logical, Text("save-static"));
    Put(root / "saves/zz-other" / dynamic, Text("wrong-save"));
    expect("save-static");
    Put(root / "saves/current" / dynamic, Text("save-dynamic"));
    expect("save-dynamic");
    registry["items"][BC::Fold(dynamic)] = "disabled"; saveRegistry();
    expect("save-static");
    registry["items"][BC::Fold(logical)] = "disabled"; saveRegistry();
    auto disabled = BC::Resolve(root, name, "current");
    Require(disabled && disabled->content.empty(), "disabled item resurrected by fallback");
    registry["items"].clear(); registry["pins"].clear(); saveRegistry();
    expect("overlay", "");
    expect("overlay", "../current");
    expect("overlay", ".. "); // Win32 must not trim this into a parent traversal
    fs::remove_all(root / "saves/current"); fs::remove_all(root / "overlay");
    Put(base / dynamic, Text("invalid-global-dynamic"));
    expect("external");
    Put(root / "saves/zz-other" / logical, Text("wrong-linked-save"));
    fs::create_directories(root / "overlay");
    std::error_code linkError;
    fs::create_directory_symlink(root / "saves/zz-other/prompts", root / "overlay/prompts", linkError);
    if (!linkError) {
        expect("external"); // a linked ancestor cannot turn another save into an overlay
        fs::remove(root / "overlay/prompts");
    } else {
        std::cout << "SKIP: directory-link fixture unavailable: " << linkError.message() << '\n';
    }
    fs::remove_all(root / "overlay");
    manifest(root / "external/test.pack", "test.pack", "2.0.0");
    Put(root / "external/test.pack" / logical, Text("newer-copy"));
    registry["plugins"]["test.pack"] = {{"enabled", true}, {"source", "hub"}}; saveRegistry();
    expect("newer-copy");
    manifest(pack, "test.pack", "2.0.0");
    expect("newer-copy"); // equal-version external beats hub
    registry["plugins"]["test.pack"]["source"] = "local"; saveRegistry();
    expect("package"); // equal-version local beats external
    registry["plugins"]["test.pack"]["source"] = "external"; saveRegistry();
    manifest(pack, "test.pack", "99.0.0");
    expect("newer-copy"); // an external-only record does not admit a library copy
    Put(root / "library/junk/manifest.json", R"({"id":42,"version":"1.0.0"})");
    Put(root / "external/test.bad/manifest.json", R"({"id":"test.bad","version":null})");
    registry["plugins"]["test.bad"] = {{"enabled", true}};
    registry["plugins"]["test.pack"] = "invalid-record"; saveRegistry();
    expect("external"); // malformed record cannot block a different valid provider
    registry["plugins"]["test.pack"] = {{"enabled", "true"}, {"source", "external"}}; saveRegistry();
    expect("newer-copy"); // registry repair treats invalid enabled as true
    registry["plugins"]["test.pack"]["enabled"] = true; saveRegistry();
    registry["pins"][BC::Fold(logical)] = "missing.pack"; saveRegistry();
    expect("newer-copy"); // dangling pin falls through
    registry["pins"].clear(); saveRegistry();
    fs::create_directories(root / "library" / BC::Utf8Path("\xED\x95\x9C"));
    expect("newer-copy"); // unrelated Unicode folder cannot poison a lookup
    manifest(root / "library/skyrimnet.base.update", "skyrimnet.base", "2.0.0");
    Put(root / "library/skyrimnet.base.update" / logical, Text("base-update"));
    registry["plugins"].clear(); saveRegistry();
    expect("base-update");
    manifest(root / "library/skyrimnet.base.update", "skyrimnet.base", "1.0.0");
    expect("base");
    fs::rename(root / "library/skyrimnet.base.update", root / "library/SKYRIMNET.BASE.UPDATE");
    expect("base"); // stale update loses even when its spelling sorts before shipped base
    Put(base / BC::Utf8Path("prompts/characters/\xED\x95\x9C_123.prompt"), Text("unicode"));
    auto unicode = BC::Resolve(root, "\xED\x95\x9C_123", "current");
    Require(unicode && BC::ExtractBlock(unicode->content, "summary") == "unicode", "UTF-8 path failed");
    Require(!BC::Resolve(root, "../test_actor_ABC", "current"), "unsafe template name accepted");
    Require(BC::ExtractBlock("{% block summary %}unfinished", "summary").empty(), "unterminated block extracted");
    Require(BC::ExtractBlock("{% block summary %}\r\n  {% endblock %}", "summary").empty(), "whitespace block extracted");
    Require(BC::ExtractBlock(Text("base"), "absent").empty(), "missing block extracted");
    Require(BC::CompareVersion(BC::VersionParts("1.0.0-rc.10"), BC::VersionParts("1.0.0-rc.2")) > 0, "prerelease ordering");
    Require(BC::CompareVersion(BC::VersionParts("1.0.0"), BC::VersionParts("1.0.0-rc.10")) > 0, "release ordering");
    fs::remove_all(root / "library"); fs::remove_all(root / "external"); fs::remove_all(root / "saves");
    Put(root / "prompts/characters/dynamic" / (name + ".dynamic.prompt"), Text("legacy-dynamic"));
    expect("legacy-dynamic", "");
    Put(root / "prompts/_saves/current/characters/dynamic" / (name + ".dynamic.prompt"), Text("legacy-save"));
    Put(root / "prompts/_saves/zz-other/characters/dynamic" / (name + ".dynamic.prompt"), Text("legacy-other"));
    expect("legacy-save");
    expect("legacy-other", ""); // pre-Content-Library behavior when no save API is available
    fs::create_directories(root / "library");
    expect("legacy-dynamic", ""); // modern builds do not borrow another save
    registry["items"][BC::Fold(dynamic)] = "disabled"; saveRegistry();
    expect("legacy"); // disabled dynamic cannot return through the legacy reader
    fs::remove(root / "prompts/characters" / (name + ".prompt"));
    Require(!BC::Resolve(root, name, ""), "disabled dynamic resurrected by legacy exact/fuzzy lookup");
    Put(root / "prompts/characters" / (name + ".DYNAMIC.prompt"), Text("disabled-mixed-case"));
    Require(!BC::Resolve(root, name, ""), "mixed-case dynamic resurrected by legacy fuzzy lookup");
    registry["items"].clear(); saveRegistry();
    fs::remove_all(root / "prompts");
    Put(root / "original_prompts/characters" / (name + ".prompt"), Text("original"));
    expect("original");
    fs::remove_all(root / "original_prompts");
    Put(root / "prompts/characters/jarl_test_actor_DEF.prompt", Text("legacy-fuzzy"));
    expect("legacy-fuzzy");
    fs::remove_all(root / "prompts");
    Require(!BC::Resolve(root, name, "current"), "missing bio should stay missing");
    std::cout << "PASS: library/registry priority, disabled/missing providers, pins, overlay, current-save dynamic/static, save isolation, base updates, UTF-8, legacy fallback/fuzzy, block extraction\n";
}
