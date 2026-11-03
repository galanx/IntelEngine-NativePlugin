#include "TaskHistoryDescription.h"
#include "FriendlyKey.h"
#include <iostream>
#include <stdexcept>

static void Require(bool value) { if (!value) throw std::runtime_error("task history outcome mismatch"); }
int main(int argc, char** argv) {
    if (argc == 7 && std::string_view(argv[1]) == "--describe") {
        std::cout << IntelEngine::BuildTaskHistoryDescription(argv[2], argv[3], argv[4], argv[5], argv[6]) << '\n';
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--friendly") {
        std::cout << IntelEngine::FriendlyKey(argv[2]) << '\n';
        return 0;
    }
    Require(IntelEngine::FriendlyKey("war_declaration") == "War Declaration");
    Require(IntelEngine::FriendlyKey("seek_player") == "Seek Player");
    Require(IntelEngine::FriendlyKey("npc_interaction") == "Npc Interaction");
    Require(IntelEngine::FriendlyKey("") == "");
    Require(IntelEngine::FriendlyKey("_two__words_") == " Two  Words ");
    Require(IntelEngine::FriendlyKey("already Friendly") == "Already Friendly");
    std::cout << "PASS: FriendlyKey empty, separators, word starts and existing case\n";
    using IntelEngine::BuildTaskHistoryDescription;
    const auto describe = [](const char* type, const char* result) {
        return BuildTaskHistoryDescription(type, "Nazeem", result, "", "");
    };
    for (const auto result : {"refused", "timeout", "cancelled", "aborted", "failed", ""}) {
        Require(describe("story", result) == "Tried to seek out Nazeem");
        Require(describe("story_npc", result) == "Tried to talk with Nazeem");
        Require(describe("unknown", result) == "Attempted a task involving Nazeem");
        Require(describe("deliver_message", result) == "Tried to deliver a message to Nazeem");
        Require(describe("escort_target", result) == "Attempted a task involving Nazeem");
        Require(describe("travel", result) == (std::string(result) == "timeout"
            ? "Went to Nazeem but gave up waiting" : "Tried to travel to Nazeem"));
    }
    for (const auto result : {"arrived", "player_arrived", "success"}) {
        Require(describe("travel", result) == "Traveled to Nazeem");
        Require(describe("story", result) == "Sought out Nazeem");
        Require(describe("story_npc", result) == "Went to talk with Nazeem");
    }
    Require(describe("fetch_npc", "refused") == "Looked for Nazeem but couldn't bring them");
    Require(describe("fetch_npc", "success") == "Found Nazeem and brought them back");
    Require(describe("escort_target", "success") == "Completed a task involving Nazeem");
    Require(describe("deliver_message", "delivered") == "Delivered a message to Nazeem");
    Require(describe("deliver_message", "Delivered") == "Delivered a message to Nazeem");
    Require(describe("deliver_message", "DELIVERED") == "Delivered a message to Nazeem");
    Require(describe("travel", "ARRIVED") == "Traveled to Nazeem");
    Require(describe("fetch_npc", "Success") == "Found Nazeem and brought them back");
    for (const auto location : {"", "none", "None", "NONE"}) {
        Require(BuildTaskHistoryDescription("deliver_message", "Nazeem", "delivered", "Hello", location)
            == "Delivered a message to Nazeem: 'Hello'");
    }
    Require(BuildTaskHistoryDescription("deliver_message", "Nazeem", "delivered", "Hello", "Whiterun")
        == "Delivered a message to Nazeem: 'Hello' (meeting at Whiterun)");
    std::cout << "PASS: success/failure/unknown outcomes and optional delivery meeting location\n";
}
