#pragma once

/**
 * Unicode-aware case folding and edit distance for UTF-8 names.
 * Windows API + std only (no RE/Plugin.h), so offline checks can include it.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace IntelEngine {

    namespace Utf8FoldDetail {
        inline bool IsAscii(const std::string& text) {
            return std::all_of(text.begin(), text.end(), [](unsigned char c) { return c < 0x80; });
        }

        inline std::string AsciiLower(std::string text) {
            for (char& c : text) {
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            }
            return text;
        }

        // UTF-8 -> lowercased UTF-16. False when the input is not valid UTF-8.
        inline bool LowerWide(const std::string& text, std::wstring& out) {
            const int length = static_cast<int>(text.size());
            const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, nullptr, 0);
            if (wideLength <= 0) return false;
            std::wstring wide(wideLength, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), length, wide.data(), wideLength);
            out.assign(wideLength, L'\0');
            return LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, wide.data(), wideLength,
                                 out.data(), wideLength, nullptr, nullptr, 0) == wideLength;
        }

        template <class String>
        int Levenshtein(const String& a, const String& b) {
            const size_t m = a.size();
            const size_t n = b.size();
            if (m == 0) return static_cast<int>(n);
            if (n == 0) return static_cast<int>(m);

            // Two rows instead of the full matrix
            std::vector<int> prev(n + 1);
            std::vector<int> curr(n + 1);
            for (size_t j = 0; j <= n; ++j) prev[j] = static_cast<int>(j);

            for (size_t i = 1; i <= m; ++i) {
                curr[0] = static_cast<int>(i);
                for (size_t j = 1; j <= n; ++j) {
                    const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                    curr[j] = (std::min)({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
                }
                std::swap(prev, curr);
            }
            return prev[n];
        }
    }

    /**
     * Lowercase a UTF-8 string, including Cyrillic and accented letters.
     * Falls back to ASCII-only lowercasing when the input is not valid UTF-8 or when folding
     * would change the byte length, so byte positions always line up with the original.
     */
    inline std::string FoldUtf8(const std::string& text) {
        using namespace Utf8FoldDetail;
        std::wstring wide;
        if (IsAscii(text) || !LowerWide(text, wide)) return AsciiLower(text);

        const int wideLength = static_cast<int>(wide.size());
        const int length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
        if (length != static_cast<int>(text.size())) return AsciiLower(text);
        std::string result(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, result.data(), length, nullptr, nullptr);
        return result;
    }

    /**
     * Case-insensitive Levenshtein distance counted in characters (UTF-16 code units), not bytes.
     * Falls back to bytes when either input is not valid UTF-8.
     */
    inline int Utf8Levenshtein(const std::string& a, const std::string& b) {
        using namespace Utf8FoldDetail;
        std::wstring wideA, wideB;
        if (!(IsAscii(a) && IsAscii(b)) && LowerWide(a, wideA) && LowerWide(b, wideB)) {
            return Levenshtein(wideA, wideB);
        }
        return Levenshtein(AsciiLower(a), AsciiLower(b));
    }

}  // namespace IntelEngine
