#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace IntelEngine {

// Empty = accepted. Informant rumor must be one sentence naming the subject and not starting with an -ing word.
// Legacy responses without "rumor" are checked on their gossip phrase instead (subject is prepended later).
inline std::string InformantRumorRejection(const std::string& subject, const std::string& rumor, const std::string& gossip) {
    auto lower = [](std::string v) {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return v;
    };
    const std::string text = rumor.empty() ? gossip : rumor;
    const std::string name = subject.substr(0, subject.find(" ["));  // drop "[tag]" suffix
    const std::string first = text.substr(0, text.find_first_of(" ,.;:"));
    if (text.empty()) return "rumor is empty";
    if (name.empty()) return "subject is empty";
    // whole word, same boundary as Papyrus ReplaceText: "Ria" must not match inside "Victoria" or "Riften"
    auto names = [&](const std::string& t, const std::string& w) {
        const std::string lt = lower(t), lw = lower(w);
        for (size_t at = lt.find(lw); at != std::string::npos; at = lt.find(lw, at + 1))
            if ((at == 0 || !std::isalpha((unsigned char)lt[at - 1])) && (at + lw.size() >= lt.size() || !std::isalpha((unsigned char)lt[at + lw.size()]))) return true;
        return false;
    };
    if (!rumor.empty() && !names(rumor, name)) return "rumor does not name the subject";
    // a name that merely ends in "ing" is allowed, and so are ordinary openings that happen to end in it: the rule
    // keeps "Hearing that ..." out, but it also refused "During the feast, Nazeem ..." and "King Torygg ..."
    // (keep this list the same as in Core.InformantRumorRejection)
    static const char* const plainWords[] = {"during", "something", "nothing", "anything", "everything", "king",
                                             "morning", "evening", "spring"};
    const std::string lowerFirst = lower(first);
    const bool plainWord = std::any_of(std::begin(plainWords), std::end(plainWords),
                                       [&](const char* w) { return lowerFirst == w; });
    if (!plainWord && first.size() > 3 && lowerFirst.compare(first.size() - 3, 3, "ing") == 0 && lower(name).rfind(lowerFirst, 0) != 0)
        return "rumor starts with an -ing word";
    return "";
}

}  // namespace IntelEngine
