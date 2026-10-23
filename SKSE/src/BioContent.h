#pragma once
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace IntelEngine::BioContent {
    namespace fs = std::filesystem;
    struct Bio { fs::path path; std::string content; };
    inline fs::path Utf8Path(std::string_view text) {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }
    inline bool SafeSegment(std::string_view text) {
        return !text.empty() && text.find_first_of("/\\:<>\"|?*") == text.npos &&
               text.find('\0') == text.npos && text.back() != '.' && text.back() != ' ';
    }
    inline std::optional<Bio> Read(const fs::path& path, const fs::path& boundary = {}) {
        std::error_code ec;
        if (!boundary.empty()) {
            for (auto parent = path.parent_path(); parent != boundary && !parent.empty();) {
                if (fs::is_symlink(fs::symlink_status(parent, ec)) || ec) return {};
                auto next = parent.parent_path();
                if (next == parent) return {};
                parent = std::move(next);
            }
        }
        if (!fs::is_regular_file(path, ec) || fs::is_symlink(fs::symlink_status(path, ec))) return {};
        std::ifstream file(path, std::ios::binary);
        if (!file) return {};
        std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (file.bad()) return {};
        return Bio{path, std::move(content)};
    }
    // Fresh iterators at each level preserve MO2's merged directory view.
    inline std::vector<fs::path> Directories(const fs::path& root) {
        std::vector<fs::path> paths;
        std::error_code ec;
        fs::directory_iterator it(root, ec), end;
        while (!ec && it != end) {
            if (it->is_directory(ec) && !it->is_symlink(ec)) paths.push_back(it->path());
            it.increment(ec);
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }
    inline std::string ExtractBlock(std::string_view content, std::string_view name) {
        const auto tag = "{% block " + std::string(name) + " %}";
        auto start = content.find(tag);
        if (start == content.npos) return {};
        start += tag.size();
        auto end = content.find("{% endblock %}", start);
        if (end == content.npos) return {};
        auto block = content.substr(start, end - start);
        auto first = block.find_first_not_of(" \n\r");
        if (first == block.npos) return {};
        return std::string(block.substr(first, block.find_last_not_of(" \n\r") - first + 1));
    }
    inline std::optional<Bio> ResolveLegacy(const fs::path& root, const std::string& name,
                                           const std::string& saveId, bool modern) {
        std::vector<fs::path> saves;
        if (SafeSegment(saveId)) saves = {root / "prompts/_saves" / Utf8Path(saveId)};
        else if (!modern) { saves = Directories(root / "prompts/_saves"); std::reverse(saves.begin(), saves.end()); }
        const auto chars = root / "prompts/characters", original = root / "original_prompts/characters";
        const auto filename = Utf8Path(name + ".prompt"), dynamic = Utf8Path(name + ".dynamic.prompt");
        for (const auto& save : saves) if (auto bio = Read(save / "characters/dynamic" / dynamic)) return bio;
        if (auto bio = Read(chars / "dynamic" / dynamic)) return bio;
        if (auto bio = Read(chars / filename)) return bio;
        if (auto bio = Read(original / filename)) return bio;
        for (const auto& save : saves) if (auto bio = Read(save / "characters" / filename)) return bio;
        auto base = name;
        auto suffix = base.rfind('_');
        if (suffix != base.npos && suffix > 0 && suffix + 1 < base.size() &&
            base.find_first_not_of("0123456789abcdefABCDEF", suffix + 1) == base.npos) base.resize(suffix);
        std::vector<fs::path> dirs{chars / "dynamic", chars, original};
        for (const auto& save : saves) { dirs.push_back(save / "characters/dynamic"); dirs.push_back(save / "characters"); }
        const auto wideBase = Utf8Path(base).wstring();
        for (const auto& dir : dirs) {
            std::error_code ec;
            fs::directory_iterator it(dir, ec), end;
            while (!ec && it != end) {
                const auto filenameWide = it->path().filename().wstring();
                if (filenameWide.find(wideBase) != filenameWide.npos && filenameWide.ends_with(L".prompt")) {
                    if (auto bio = Read(it->path())) return bio;
                }
                it.increment(ec);
            }
        }
        return {};
    }
    inline std::optional<Bio> Resolve(const fs::path& contentRoot, const std::string& name, const std::string& saveId) {
        if (!SafeSegment(name)) return {};
        auto root = contentRoot.lexically_normal();
        if (root.has_relative_path() && root.filename().empty()) root = root.parent_path();
        std::vector<fs::path> layers;
        if (SafeSegment(saveId)) layers.push_back(root / "saves" / Utf8Path(saveId));
        layers.push_back(root / "overlay");
        for (const auto& dir : Directories(root / "external")) layers.push_back(dir);
        auto packages = Directories(root / "library");
        std::stable_partition(packages.begin(), packages.end(), [](const fs::path& path) {
            auto name = path.filename().wstring();
            for (auto& c : name) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
            return name != L"skyrimnet.base";
        });
        layers.insert(layers.end(), packages.begin(), packages.end());
        // ponytail: sorted packages can differ from SkyrimNet registry priority; use a public content API if one ships.
        for (const auto& relative : {Utf8Path("prompts/characters/dynamic/" + name + ".dynamic.prompt"),
                                     Utf8Path("prompts/characters/" + name + ".prompt")}) {
            for (const auto& layer : layers) if (auto bio = Read(layer / relative, root)) return bio;
        }
        std::error_code ec;
        bool modern = fs::is_directory(root / "library", ec) || fs::is_directory(root / "overlay", ec) ||
                      fs::is_directory(root / "saves", ec) || fs::is_directory(root / "external", ec);
        return ResolveLegacy(root, name, saveId, modern);
    }
}
