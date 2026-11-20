#pragma once

// IntelEngine: the faction editor's words (FactionEditor.h): what may be written into factions.yaml, the text of one
// faction, and the file with its "factions:" section replaced. Pure logic: no game types, no file access, so
// SKSE/tests/FactionEditorCheck.cpp can run it.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace IntelEngine::FactionEditor {

    inline constexpr const char* kTypes[] = {"military", "guild", "political"};
    inline constexpr const char* kStyles[] = {"", "brawl", "sabotage", "assassination", "proxy"};
    inline constexpr const char* kHolds[] = {"Whiterun", "Falkreath", "Markarth", "Solitude", "Morthal", "Dawnstar", "Winterhold", "Windhelm", "Riften"};

    namespace text {
        inline std::string Text(const nlohmann::json& j, const char* key) {
            const auto it = j.find(key);
            return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
        }
        inline int Number(const nlohmann::json& j, const char* key, int fallback) {
            const auto it = j.find(key);
            if (it == j.end()) return fallback;
            if (it->is_number()) return static_cast<int>(it->get<double>());
            if (it->is_string()) {
                try {
                    return std::stoi(it->get<std::string>());
                } catch (...) {
                    return fallback;
                }
            }
            return fallback;
        }
        inline std::vector<std::string> List(const nlohmann::json& j, const char* key) {
            std::vector<std::string> out;
            const auto it = j.find(key);
            if (it == j.end() || !it->is_array()) return out;
            for (const auto& v : *it) {
                if (!v.is_string()) continue;
                const auto s = v.get<std::string>();
                const auto first = s.find_first_not_of(" \t"), last = s.find_last_not_of(" \t");
                if (first == std::string::npos) continue;
                out.push_back(s.substr(first, last - first + 1));
            }
            return out;
        }
        // The file's reader takes a value between double quotes as it stands and knows no escapes: a quote, a
        // backslash or a line break inside a value cannot be written.
        inline bool Writable(const std::string& s) { return s.find_first_of("\"\\\r\n") == std::string::npos; }
        inline std::string Quoted(const std::string& s) { return "\"" + s + "\""; }
        inline std::string Array(const std::vector<std::string>& values) {
            std::string out = "[";
            for (std::size_t i = 0; i < values.size(); ++i) out += (i ? ", " : "") + Quoted(values[i]);
            return out + "]";
        }
    }

    /** Why this faction cannot be written (a missing or malformed id, a name with a quote in it...), empty when fine. */
    inline std::string Refusal(const nlohmann::json& faction) {
        using namespace text;
        if (!faction.is_object()) return "not a faction";
        const auto id = Text(faction, "id");
        if (id.empty()) return "a faction has no id";
        if (id.size() > 60) return id + ": the id is too long";
        for (const char c : id) {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return id + ": an id is made of letters, digits and _ only";
        }
        if (id.rfind("PE_", 0) == 0) return id + ": ids that begin with PE_ belong to Political Engine's realms";
        if (Text(faction, "name").empty()) return id + ": it has no name";
        const auto type = Text(faction, "type");
        if (std::none_of(std::begin(kTypes), std::end(kTypes), [&](const char* t) { return type == t; })) return id + ": its type is military, guild or political";
        for (const char* key : {"name", "hold", "skyrim_faction_id", "conflict_style", "soldier_template", "prison_location"}) {
            if (!Writable(Text(faction, key))) return id + ": \"" + key + "\" may not hold a quote, a backslash or a line break";
        }
        for (const char* key : {"leader_names", "rivals", "allies", "free_after"}) {
            for (const auto& v : List(faction, key)) {
                if (!Writable(v) || v.find(',') != std::string::npos) return id + ": an entry of \"" + key + "\" may not hold a quote, a backslash or a comma";
            }
        }
        return {};
    }

    /** One faction as the file has it. */
    inline std::string FactionYaml(const nlohmann::json& f) {
        using namespace text;
        std::ostringstream out;
        out << "  - id: " << Quoted(Text(f, "id")) << "\n";
        out << "    name: " << Quoted(Text(f, "name")) << "\n";
        out << "    type: " << Quoted(Text(f, "type")) << "\n";
        out << "    hold: " << Quoted(Text(f, "hold")) << "\n";
        if (const auto v = Text(f, "skyrim_faction_id"); !v.empty()) out << "    skyrim_faction_id: " << Quoted(v) << "\n";
        out << "    leader_names: " << Array(List(f, "leader_names")) << "\n";
        out << "    rivals: " << Array(List(f, "rivals")) << "\n";
        out << "    allies: " << Array(List(f, "allies")) << "\n";
        if (const int v = std::clamp(Number(f, "base_army_strength", 0), 0, 500); v > 0) out << "    base_army_strength: " << v << "\n";
        out << "    war_threshold: " << std::clamp(Number(f, "war_threshold", -50), -100, 0) << "\n";
        if (const auto v = Text(f, "conflict_style"); !v.empty()) out << "    conflict_style: " << Quoted(v) << "\n";
        if (const auto v = Text(f, "soldier_template"); !v.empty()) out << "    soldier_template: " << Quoted(v) << "\n";
        if (f.contains("free_after") && f["free_after"].is_array()) out << "    free_after: " << Array(List(f, "free_after")) << "\n";
        if (const auto v = Text(f, "prison_location"); !v.empty()) {
            out << "    prison_location: " << Quoted(v) << "\n";
            double m[3] = {0.0, 0.0, 0.0};
            if (const auto it = f.find("prison_marker"); it != f.end() && it->is_array() && it->size() == 3) {
                for (std::size_t i = 0; i < 3; ++i) if ((*it)[i].is_number()) m[i] = (*it)[i].get<double>();
            }
            out << "    prison_marker: [" << m[0] << ", " << m[1] << ", " << m[2] << "]\n";
        }
        if (const int v = Number(f, "pe_side", -1); v >= 0 && v <= 3) out << "    pe_side: " << v << "\n";
        return out.str();
    }

    /** The file with its "factions:" section replaced by `factionsSection` (which begins with the line "factions:").
     *  The section runs to the next key at the start of a line (default_relations:, war_allowed:...); the comment
     *  lines right above that key are its heading and stay with it. A file without the section gets it first. */
    inline std::string Splice(const std::string& file, const std::string& factionsSection) {
        std::vector<std::string> lines;
        {
            std::istringstream in(file);
            std::string line;
            while (std::getline(in, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
            }
        }
        const auto topKey = [](const std::string& line) {
            if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#' || line[0] == '-') return false;
            return line.find(':') != std::string::npos;
        };
        std::size_t begin = lines.size(), end = lines.size();
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].rfind("factions:", 0) == 0) {
                begin = i;
                break;
            }
        }
        std::string out;
        if (begin == lines.size()) {
            out = factionsSection;
            if (!out.empty() && out.back() != '\n') out += "\n";
            if (!lines.empty()) out += "\n";
            for (const auto& line : lines) out += line + "\n";
            return out;
        }
        for (std::size_t i = begin + 1; i < lines.size(); ++i) {
            if (topKey(lines[i])) {
                end = i;
                break;
            }
        }
        std::size_t tail = end;
        while (tail > begin + 1 && (lines[tail - 1].empty() || lines[tail - 1][0] == '#')) --tail;
        // One empty line between the section and what follows, however many there were: saving twice adds none.
        while (tail < end && lines[tail].empty()) ++tail;
        for (std::size_t i = 0; i < begin; ++i) out += lines[i] + "\n";
        out += factionsSection;
        if (!out.empty() && out.back() != '\n') out += "\n";
        if (tail < lines.size()) out += "\n";
        for (std::size_t i = tail; i < lines.size(); ++i) out += lines[i] + "\n";
        return out;
    }
}
