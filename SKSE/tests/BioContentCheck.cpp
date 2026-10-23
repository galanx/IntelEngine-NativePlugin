// Standalone C++20, no game ABI: cl /nologo /std:c++20 /EHsc /W4 /WX /I SKSE/src SKSE/tests/BioContentCheck.cpp
#include "BioContent.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace BC = IntelEngine::BioContent;
namespace fs = std::filesystem;
static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void Put(const fs::path& path, const std::string& label) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path);
    file << "{% block summary %}\n " << label << " \n{% endblock %}\n"
            "{% block relationships %}\n Ally of " << label << ". \n{% endblock %}";
    Require(static_cast<bool>(file), "fixture write failed");
}
int main() {
    const auto root = fs::temp_directory_path() / ("ie-bio-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{root};
    const std::string name = "test_actor_ABC";
    const auto dynamic = fs::path("prompts/characters/dynamic") / (name + ".dynamic.prompt");
    const auto fixed = fs::path("prompts/characters") / (name + ".prompt");
    const std::vector<fs::path> layers{"saves/current", "overlay", "external/a.pack", "external/z.pack",
                                      "library/a.pack", "library/z.pack", "library/skyrimnet.base"};
    const auto expect = [&](const std::string& label, const std::string& save = "current") {
        auto bio = BC::Resolve(root, name, save);
        Require(bio && BC::ExtractBlock(bio->content, "summary") == label, ("wrong summary: " + label).c_str());
        Require(BC::ExtractBlock(bio->content, "relationships") == "Ally of " + label + ".", "wrong relationships");
    };
    // Create in reverse order to distinguish name order from enumeration/insertion order.
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) Put(root / *it / fixed, it->generic_string());
    Put(root / "saves/other" / dynamic, "wrong-save");
    expect("overlay", "");
    for (const auto& layer : layers) {
        expect(layer.generic_string());
        fs::remove(root / layer / fixed);
    }
    Put(root / layers.front() / fixed, "save-static");
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) Put(root / *it / dynamic, it->generic_string());
    for (const auto& layer : layers) {
        expect(layer.generic_string()); // even the lowest dynamic precedes the highest static
        fs::remove(root / layer / dynamic);
    }
    expect("save-static");
    Put(root / "prompts/characters/dynamic" / (name + ".dynamic.prompt"), "legacy-dynamic");
    expect("save-static"); // legacy is considered only after both modern passes miss
    fs::remove(root / layers.front() / fixed);
    expect("legacy-dynamic");
    for (const auto& folder : {"saves", "overlay", "external", "library"}) fs::remove_all(root / folder);
    Put(root / "prompts/_saves/current/characters/dynamic" / (name + ".dynamic.prompt"), "legacy-current");
    Put(root / "prompts/_saves/zz-other/characters/dynamic" / (name + ".dynamic.prompt"), "legacy-other");
    expect("legacy-current");
    expect("legacy-other", "");
    fs::remove_all(root / "prompts/_saves");
    expect("legacy-dynamic");
    fs::remove_all(root / "prompts/characters/dynamic");
    Put(root / "prompts/characters" / (name + ".prompt"), "legacy-static");
    Put(root / "original_prompts/characters" / (name + ".prompt"), "original");
    Put(root / "prompts/_saves/current/characters" / (name + ".prompt"), "legacy-save-static");
    expect("legacy-static");
    fs::remove(root / "prompts/characters" / (name + ".prompt"));
    expect("original");
    fs::remove_all(root / "original_prompts");
    expect("legacy-save-static");
    fs::remove_all(root / "prompts/_saves");
    Put(root / "prompts/characters/jarl_test_actor_DEF.prompt", "fuzzy");
    expect("fuzzy");
    Require(BC::ExtractBlock("{% block summary %}unfinished", "summary").empty(), "unterminated block");
    Require(BC::ExtractBlock("{% block summary %}\r\n  {% endblock %}", "summary").empty(), "whitespace block");
    Require(BC::ExtractBlock("plain text", "relationships").empty(), "missing block");
    std::cout << "PASS: dynamic then static; current-save, overlay, sorted external/library, base last; legacy exact/fuzzy; block extraction\n";
}
