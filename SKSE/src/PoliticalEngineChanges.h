#pragma once
#include <nlohmann/json.hpp>
namespace IntelEngine {
inline const nlohmann::json& PEField(const nlohmann::json& value, const char* key) {
    static const nlohmann::json missing;
    const auto it = value.is_object() ? value.find(key) : value.end();
    return it == value.end() ? missing : *it;
}
inline nlohmann::json PEComparable(const nlohmann::json& snapshot) {
    using nlohmann::json;
    json result = json::object();
    const auto& governments = PEField(PEField(snapshot, "campaign080"), "governments");
    auto publicRealm = [&](const json& id) {
        if (!id.is_number_integer() || id < 0 || id > 9 || !governments.is_array()) return false;
        const auto index = id.get<std::size_t>();
        return index < governments.size() && PEField(governments[index], "kind").is_number_integer() &&
            PEField(governments[index], "kind") != 2;
    };
    const auto& regions = PEField(PEField(snapshot, "campaign070"), "regions");
    if (regions.is_array()) result["regions"] = json::object();
    if (regions.is_array()) for (const auto& row : regions) {
        const auto& id = PEField(row, "district_id");
        const auto& owner = PEField(row, "owner_realm_id");
        const auto& name = PEField(row, "name");
        if (id.is_number_integer() && id >= 0 && publicRealm(owner) && name.is_string())
            result["regions"][id.dump()] = {{"owner", owner}, {"name", name}};
    }
    const auto& rulers = PEField(snapshot, "rulers");
    if (rulers.is_array()) result["rulers"] = json::object();
    if (rulers.is_array()) for (const auto& row : rulers) {
        const auto& id = PEField(row, "realm_id");
        const auto& actor = PEField(row, "actor_id");
        if (publicRealm(id) && PEField(row, "present") == true && actor.is_number_integer() && actor > 0)
            result["rulers"][id.dump()] = actor;
    }
    const auto& wars = PEField(PEField(PEField(snapshot, "campaign080"), "wars"), "wars");
    if (wars.is_array()) result["wars"] = json::object();
    if (wars.is_array()) for (const auto& row : wars) {
        const auto& id = PEField(row, "id");
        const auto& phase = PEField(row, "phase");
        const auto& first = PEField(row, "first");
        const auto& second = PEField(row, "second");
        const auto& settlement = PEField(row, "settlement");
        const auto& applied = PEField(row, "settlement_applied");
        if (phase == 3 && !(applied.is_null() || applied == true)) continue;
        if (id.is_number_integer() && id >= 0 && publicRealm(first) && publicRealm(second) &&
            phase.is_number_integer() && phase >= 0 && phase <= 3 && PEField(settlement, "outcome") != "puppet")
            result["wars"][id.dump()] = {{"phase", phase}, {"first", first}, {"second", second}};
    }
    const auto& factions = PEField(snapshot, "factions");
    if (factions.is_array()) for (const auto& row : factions) {
        const auto& id = PEField(row, "realm_id");
        if (publicRealm(id) && PEField(row, "name").is_string()) result["realms"][id.dump()] = PEField(row, "name");
    }
    result["revision"] = PEField(snapshot, "revision");
    return result;
}
inline nlohmann::json PEDiff(const nlohmann::json& before, const nlohmann::json& after) {
    using nlohmann::json;
    json changes = json::array(), captures = json::array();
    const auto& previousRegions = PEField(before, "regions");
    const auto& regions = PEField(after, "regions");
    if (regions.is_object()) for (const auto& [id, row] : regions.items()) {
        const auto& previous = PEField(previousRegions, id.c_str());
        if (previous.is_object() && PEField(previous, "owner") != PEField(row, "owner"))
            captures.push_back({{"district", id}, {"name", PEField(row, "name")},
                                {"from", PEField(previous, "owner")}, {"to", PEField(row, "owner")}});
    }
    if (!captures.empty()) changes.push_back({{"type", "pe_territory"}, {"changes", std::move(captures)}});
    const auto& previousWars = PEField(before, "wars");
    const auto& wars = PEField(after, "wars");
    if (wars.is_object()) for (const auto& [id, row] : wars.items()) {
        const auto& previous = PEField(previousWars, id.c_str());
        const auto& phase = PEField(row, "phase");
        if (previousWars.is_object() && PEField(previous, "phase") != phase &&
            (phase == 1 || (previous.is_object() && phase == 3)))
            changes.push_back({{"type", "pe_campaign"}, {"war", id}, {"state", row}});
    }
    const auto& previousRulers = PEField(before, "rulers");
    const auto& rulers = PEField(after, "rulers");
    if (rulers.is_object()) for (const auto& [id, actor] : rulers.items()) {
        const auto& previous = PEField(previousRulers, id.c_str());
        if (previous.is_number_integer() && previous != actor)
            changes.push_back({{"type", "pe_ruler"}, {"realm", id}, {"actor", actor}});
    }
    return changes;
}
inline std::string PEEventText(const nlohmann::json& change) {
    const auto realm = [&](const nlohmann::json& id) {
        const auto& name = PEField(PEField(change, "realms"), id.is_string() ? id.get<std::string>().c_str() : id.dump().c_str());
        return name.is_string() ? name.get<std::string>() : std::string{};
    };
    const auto type = PEField(change, "type");
    if (type == "pe_ruler") {
        auto name = realm(PEField(change, "realm"));
        return name.empty() ? "" : name + " appointed a new ruler.";
    }
    if (type == "pe_campaign") {
        const auto& war = PEField(change, "state");
        auto first = realm(PEField(war, "first")), second = realm(PEField(war, "second"));
        if (first.empty() || second.empty()) return "";
        return first + " and " + second + (PEField(war, "phase") == 1 ? " went to war." : " ended their war.");
    }
    std::string text;
    if (type == "pe_territory") for (const auto& capture : PEField(change, "changes")) {
        auto from = realm(PEField(capture, "from")), to = realm(PEField(capture, "to"));
        const auto& name = PEField(capture, "name");
        if (!from.empty() && !to.empty() && name.is_string())
            text += (text.empty() ? "" : " ") + to + " took " + name.get<std::string>() + " from " + from + ".";
    }
    return text;
}
}
