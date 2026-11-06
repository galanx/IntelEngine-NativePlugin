#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    const nlohmann::json state = {{"name", std::string("S\xE9nn")}, {"count", 7}};
    const auto dumped = state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    const auto parsed = nlohmann::json::parse(dumped);
    if (parsed.at("name") != std::string("S\xEF\xBF\xBDnn") || parsed.at("count") != 7) {
        throw std::runtime_error("Dashboard UTF-8 replacement check failed");
    }
    std::cout << "DashboardUtf8DumpCheck: PASS (invalid byte replaced with U+FFFD; state preserved)\n";
}
