#pragma once

#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace IntelEngine::BioContent {
    namespace fs = std::filesystem;

    struct Bio {
        fs::path path;
        std::string content;
    };

    inline fs::path Utf8Path(std::string_view text) {
        return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    }

    inline std::string Fold(std::string text) {
        for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return text;
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

    inline nlohmann::json Json(const fs::path& path, const fs::path& boundary = {}) {
        auto file = Read(path, boundary);
        if (!file) return nlohmann::json::object();
        auto value = nlohmann::json::parse(file->content, nullptr, false);
        return value.is_object() ? value : nlohmann::json::object();
    }

    inline std::string String(const nlohmann::json& object, const char* key) {
        auto it = object.find(key);
        return it != object.end() && it->is_string() ? it->get<std::string>() : "";
    }

    inline bool Flag(const nlohmann::json& object, const char* key, bool fallback) {
        auto it = object.find(key);
        return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
    }

    // Fresh iterators at each level are required for MO2's merged virtual filesystem.
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

    // Semver precedence, including numeric prerelease identifiers; build metadata has no rank.
    inline std::vector<std::string> VersionParts(const std::string& version) {
        static const std::regex grammar(
            R"((0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?)");
        std::smatch match;
        if (!std::regex_match(version, match, grammar)) return {};
        std::vector<std::string> parts{match[1], match[2], match[3]};
        if (match[4].matched) {
            std::string pre = match[4];
            size_t start = 0;
            do {
                auto end = pre.find('.', start);
                auto part = pre.substr(start, end - start);
                if (part.size() > 1 && part[0] == '0' && part.find_first_not_of("0123456789") == part.npos) return {};
                parts.push_back(std::move(part));
                if (end == pre.npos) break;
                start = end + 1;
            } while (true);
        }
        return parts;
    }

    inline int CompareVersion(const std::vector<std::string>& a, const std::vector<std::string>& b) {
        for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
            if (i == a.size()) return i == 3 ? 1 : -1;
            if (i == b.size()) return i == 3 ? -1 : 1;
            bool an = a[i].find_first_not_of("0123456789") == a[i].npos;
            bool bn = b[i].find_first_not_of("0123456789") == b[i].npos;
            if (an != bn) return an ? -1 : 1;
            if (an && a[i].size() != b[i].size()) return a[i].size() > b[i].size() ? 1 : -1;
            if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
        }
        return 0;
    }

    inline nlohmann::json FoldKeys(const nlohmann::json& object) {
        auto result = nlohmann::json::object();
        if (object.is_object()) for (const auto& [key, value] : object.items()) result.emplace(Fold(key), value);
        return result;
    }

    inline std::optional<Bio> ResolveLibrary(const fs::path& root, const std::string& name,
                                            const std::string& saveId, bool& dynamicAllowed) {
        const auto registry = Json(root / "content-registry.json", root);
        const auto records = FoldKeys(registry.value("plugins", nlohmann::json::object()));
        const auto pins = FoldKeys(registry.value("pins", nlohmann::json::object()));
        const auto items = FoldKeys(registry.value("items", nlohmann::json::object()));
        struct Layer { fs::path path; std::vector<std::string> version; int rank; };
        std::map<std::string, Layer> layers;
        for (const auto& folder : {"library", "external"}) {
            for (const auto& path : Directories(root / folder)) {
                const auto utf8 = path.filename().generic_u8string();
                auto dir = Fold(std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
                if (dir.starts_with('.')) continue;
                auto manifest = Json(path / "manifest.json", root);
                auto id = Fold(String(manifest, "id"));
                static const std::regex idGrammar(R"([a-z0-9_-]+\.[a-z0-9_-]+)");
                if (!std::regex_match(id, idGrammar)) continue;
                bool base = id == "skyrimnet.base";
                bool external = std::string_view(folder) == "external";
                if (external && id.starts_with("skyrimnet.")) continue;
                if (base ? (external || (dir != id && dir != "skyrimnet.base.update")) : dir != id) continue;
                if (!base && (!records.contains(id) || !records[id].is_object() ||
                    !Flag(records[id], "enabled", true) || Flag(records[id], "missing", false))) continue;
                auto source = !base ? String(records[id], "source") : "hub";
                if (source != "local" && source != "external") source = "hub";
                if (!base && !external && source != "hub" && source != "local") continue;
                auto version = VersionParts(String(manifest, "version"));
                if (version.empty()) continue;
                int rank = external ? 1 : (source == "local" ? 0 : 2);
                auto held = layers.find(id);
                if (held != layers.end()) {
                    int order = CompareVersion(version, held->second.version);
                    bool winsTie = rank < held->second.rank || (base && dir == "skyrimnet.base");
                    if (order < 0 || (order == 0 && !winsTie)) continue;
                }
                layers[id] = Layer{path, std::move(version), rank};
            }
        }
        std::vector<std::string> order;
        auto priority = registry.value("priority", nlohmann::json::array());
        if (priority.is_array()) for (const auto& entry : priority) {
            if (!entry.is_string()) continue;
            auto id = Fold(entry.get<std::string>());
            if (id != "skyrimnet.base" && layers.contains(id) && std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
        }
        for (const auto& [id, layer] : layers) {
            if (id != "skyrimnet.base" && std::find(order.begin(), order.end(), id) == order.end()) order.push_back(id);
        }
        if (layers.contains("skyrimnet.base")) order.push_back("skyrimnet.base");
        const auto resolve = [&](const std::string& logical, bool dynamic) -> std::optional<Bio> {
            const auto key = Fold(logical);
            // A disabled logical item must not be resurrected from a lower layer or legacy root.
            if (items.contains(key) && items[key] == "disabled") return Bio{};
            const auto relative = Utf8Path(logical);
            if (SafeSegment(saveId)) if (auto bio = Read(root / "saves" / Utf8Path(saveId) / relative, root)) return bio;
            if (dynamic) return {};
            if (pins.contains(key) && pins[key].is_string()) {
                auto pinned = layers.find(Fold(pins[key].get<std::string>()));
                if (pinned != layers.end()) if (auto bio = Read(pinned->second.path / relative, root)) return bio;
            }
            if (auto bio = Read(root / "overlay" / relative, root)) return bio;
            for (const auto& id : order) if (auto bio = Read(layers.at(id).path / relative, root)) return bio;
            return {};
        };
        auto dynamic = resolve("prompts/characters/dynamic/" + name + ".dynamic.prompt", true);
        dynamicAllowed = !dynamic || !dynamic->path.empty();
        if (dynamic && !dynamic->path.empty()) return dynamic;
        return resolve("prompts/characters/" + name + ".prompt", false);
    }

    inline std::optional<Bio> ResolveLegacy(const fs::path& root, const std::string& name,
                                           const std::string& saveId, bool modern, bool dynamicAllowed) {
        std::vector<fs::path> saves;
        // Never enumerate other playthroughs when the current save is known or the layout is modern.
        if (SafeSegment(saveId)) saves = {root / "prompts/_saves" / Utf8Path(saveId)};
        else if (!modern) {
            saves = Directories(root / "prompts/_saves");
            std::reverse(saves.begin(), saves.end());
        }
        const auto chars = root / "prompts/characters";
        const auto original = root / "original_prompts/characters";
        const auto filename = Utf8Path(name + ".prompt");
        const auto dynamic = Utf8Path(name + ".dynamic.prompt");
        if (dynamicAllowed) {
            for (const auto& save : saves) if (auto bio = Read(save / "characters/dynamic" / dynamic)) return bio;
            if (auto bio = Read(chars / "dynamic" / dynamic)) return bio;
        }
        if (auto bio = Read(chars / filename)) return bio;
        if (auto bio = Read(original / filename)) return bio;
        for (const auto& save : saves) if (auto bio = Read(save / "characters" / filename)) return bio;
        auto base = name;
        auto suffix = base.rfind('_');
        if (suffix != base.npos && suffix > 0 && suffix + 1 < base.size() &&
            base.find_first_not_of("0123456789abcdefABCDEF", suffix + 1) == base.npos) base.resize(suffix);
        std::vector<fs::path> dirs{chars, original};
        if (dynamicAllowed) dirs.insert(dirs.begin(), chars / "dynamic");
        for (const auto& save : saves) {
            if (dynamicAllowed) dirs.push_back(save / "characters/dynamic");
            dirs.push_back(save / "characters");
        }
        const auto wideBase = Utf8Path(base).wstring();
        for (const auto& dir : dirs) {
            std::error_code ec;
            fs::directory_iterator it(dir, ec), end;
            while (!ec && it != end) {
                const auto filenameWide = it->path().filename().wstring();
                constexpr std::wstring_view dynamicSuffix = L".dynamic.prompt";
                bool isDynamic = filenameWide.size() >= dynamicSuffix.size() &&
                    std::equal(dynamicSuffix.begin(), dynamicSuffix.end(), filenameWide.end() - dynamicSuffix.size(),
                        [](wchar_t a, wchar_t b) { return a == (b >= L'A' && b <= L'Z' ? b + (L'a' - L'A') : b); });
                if (filenameWide.find(wideBase) != filenameWide.npos && filenameWide.ends_with(L".prompt") &&
                    (dynamicAllowed || !isDynamic)) {
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
        bool dynamicAllowed = true;
        if (auto bio = ResolveLibrary(root, name, saveId, dynamicAllowed)) return bio;
        std::error_code ec;
        bool modern = fs::is_directory(root / "library", ec) || fs::is_directory(root / "overlay", ec) ||
                      fs::is_directory(root / "saves", ec) || fs::is_directory(root / "external", ec);
        return ResolveLegacy(root, name, saveId, modern, dynamicAllowed);
    }
}
