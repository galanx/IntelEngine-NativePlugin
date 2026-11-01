#pragma once

#include "Utf8Truncate.h"
#include <string>
#include <string_view>

namespace IntelEngine {
inline std::string BuildTaskHistoryDescription(std::string_view type, std::string_view target,
    std::string_view result, std::string_view message, std::string_view location) {
    const bool succeeded = result == "success" || result == "arrived" || result == "player_arrived";
    std::string_view prefix;
    std::string_view suffix;
    if (type == "travel") {
        prefix = succeeded ? "Traveled to " : "Tried to travel to ";
        if (result == "timeout") {
            prefix = "Went to ";
            suffix = " but gave up waiting";
        }
    } else if (type == "fetch_npc") {
        prefix = result == "success" ? "Found " : "Looked for ";
        suffix = result == "success" ? " and brought them back" : " but couldn't bring them";
    } else if (type == "deliver_message") {
        prefix = result == "delivered" ? "Delivered a message to " : "Tried to deliver a message to ";
    } else if (type == "search_for_actor") {
        prefix = "Searched for ";
    } else if (type == "story") {
        prefix = succeeded ? "Sought out " : "Tried to seek out ";
    } else if (type == "story_npc") {
        prefix = succeeded ? "Went to talk with " : "Tried to talk with ";
    } else {
        prefix = succeeded || result == "delivered" ? "Completed a task involving " : "Attempted a task involving ";
    }
    std::string desc(prefix);
    desc += target;
    desc += suffix;
    if (type == "deliver_message" && result == "delivered") {
        if (!message.empty()) {
            desc += ": '";
            desc += message.substr(0, Utf8PrefixBytes(message, 80));
            if (message.size() > 80) desc += "...";
            desc += "'";
        }
        if (!location.empty() && location != "none" && location != "None" && location != "NONE") {
            desc.append(" (meeting at ").append(location).append(")");
        }
    }
    return desc;
}
}  // namespace IntelEngine
