#pragma once

// When one name may stand for another. The lookups took any name that contained the one asked for, or was contained
// in it, or was three letters off: "Garth" of another mod came for "Gollgarth", a modded character with "tsun" inside
// their name answered for Tsun, and a creature called "Edan's Shadow" drew people meant for someone else. A name
// stands for another as whole words only, and a slip of the pen is measured by how long the name is. By structure
// alone (letters, word boundaries, lengths), so it holds in every language. Pure logic, checked by NameMatchCheck.

#include <algorithm>
#include <cctype>
#include <string>

namespace IntelEngine::NameMatch {

    // Part of a word: a letter or digit, any byte of a letter outside ASCII (UTF-8), or an apostrophe inside a name.
    inline bool WordChar(unsigned char c) { return std::isalnum(c) != 0 || c >= 0x80 || c == '\''; }

    // `needle` stands in `hay` as whole words: "jon" and "battle-born" in "jon battle-born", "lydia" in "lydia the
    // housecarl"; never "garth" in "gollgarth", "tsun" in "kaitsun", or "edan" in "edan's shadow". Both in lower case.
    inline bool WholeWords(const std::string& hay, const std::string& needle) {
        if (needle.empty() || needle.size() > hay.size()) return false;
        for (auto at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) {
            const auto end = at + needle.size();
            const bool left = at == 0 || !WordChar(static_cast<unsigned char>(hay[at - 1]));
            const bool right = end == hay.size() || !WordChar(static_cast<unsigned char>(hay[end]));
            if (left && right) return true;
        }
        return false;
    }

    // One of the two is part of the other, as whole words.
    inline bool PartOf(const std::string& a, const std::string& b) { return WholeWords(a, b) || WholeWords(b, a); }

    // How many letters may be wrong before it is another name: none in a name of up to four letters, one up to seven,
    // two up to eleven, then three; never more than the setting allows.
    inline int Slips(std::size_t length, int most) {
        const int byLength = length <= 4 ? 0 : length <= 7 ? 1 : length <= 11 ? 2 : 3;
        return std::min(most, byLength);
    }
    inline int Slips(const std::string& a, const std::string& b, int most) { return Slips(std::min(a.size(), b.size()), most); }
}
