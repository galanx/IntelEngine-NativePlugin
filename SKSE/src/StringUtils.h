#pragma once

/**
 * String Utilities
 *
 * High-performance string operations that would be too slow in Papyrus.
 */

#include "Plugin.h"

#include <string>
#include <vector>
#include <algorithm>

namespace IntelEngine::StringUtils {

    /**
     * Convert string to lowercase.
     */
    inline std::string ToLowerStd(const std::string& text) {
        std::string result = text;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    inline RE::BSFixedString ToLower(const std::string& text) {
        return RE::BSFixedString(ToLowerStd(text));
    }

    /**
     * Convert string to uppercase.
     */
    inline std::string ToUpperStd(const std::string& text) {
        std::string result = text;
        std::transform(result.begin(), result.end(), result.begin(),
                       [](unsigned char c) { return std::toupper(c); });
        return result;
    }

    inline RE::BSFixedString ToUpper(const std::string& text) {
        return RE::BSFixedString(ToUpperStd(text));
    }

    /**
     * Check if string contains substring (case-insensitive).
     */
    inline bool Contains(const std::string& haystack, const std::string& needle) {
        if (needle.empty()) return true;
        if (haystack.empty()) return false;

        std::string lowerHay = ToLowerStd(haystack);
        std::string lowerNeedle = ToLowerStd(needle);

        return lowerHay.find(lowerNeedle) != std::string::npos;
    }

    /**
     * Check if string starts with prefix (case-insensitive).
     */
    inline bool StartsWith(const std::string& text, const std::string& prefix) {
        if (prefix.empty()) return true;
        if (text.length() < prefix.length()) return false;

        std::string lowerText = ToLowerStd(text);
        std::string lowerPrefix = ToLowerStd(prefix);

        return lowerText.compare(0, lowerPrefix.length(), lowerPrefix) == 0;
    }

    /**
     * Check if string ends with suffix (case-insensitive).
     */
    inline bool EndsWith(const std::string& text, const std::string& suffix) {
        if (suffix.empty()) return true;
        if (text.length() < suffix.length()) return false;

        std::string lowerText = ToLowerStd(text);
        std::string lowerSuffix = ToLowerStd(suffix);

        return lowerText.compare(lowerText.length() - lowerSuffix.length(), lowerSuffix.length(), lowerSuffix) == 0;
    }

    /**
     * Calculate Levenshtein edit distance between two strings.
     * Used for fuzzy matching - lower distance = more similar.
     */
    inline int LevenshteinDistance(const std::string& a, const std::string& b) {
        const size_t m = a.length();
        const size_t n = b.length();

        if (m == 0) return static_cast<int>(n);
        if (n == 0) return static_cast<int>(m);

        // Use two rows instead of full matrix for memory efficiency
        std::vector<int> prev(n + 1);
        std::vector<int> curr(n + 1);

        // Initialize first row
        for (size_t j = 0; j <= n; ++j) {
            prev[j] = static_cast<int>(j);
        }

        // Fill matrix
        for (size_t i = 1; i <= m; ++i) {
            curr[0] = static_cast<int>(i);

            for (size_t j = 1; j <= n; ++j) {
                int cost = (std::tolower(a[i - 1]) == std::tolower(b[j - 1])) ? 0 : 1;
                curr[j] = std::min({
                    prev[j] + 1,       // deletion
                    curr[j - 1] + 1,   // insertion
                    prev[j - 1] + cost // substitution
                });
            }

            std::swap(prev, curr);
        }

        return prev[n];
    }

    /**
     * Trim whitespace from both ends of string.
     */
    inline std::string TrimStd(const std::string& text) {
        const char* whitespace = " \t\n\r\f\v";
        size_t start = text.find_first_not_of(whitespace);
        if (start == std::string::npos) return "";

        size_t end = text.find_last_not_of(whitespace);
        return text.substr(start, end - start + 1);
    }

    inline RE::BSFixedString Trim(const std::string& text) {
        return RE::BSFixedString(TrimStd(text));
    }

    /**
     * Split string by delimiter.
     */
    inline std::vector<std::string> Split(const std::string& text, const std::string& delimiter) {
        std::vector<std::string> result;

        if (text.empty()) return result;
        if (delimiter.empty()) {
            result.push_back(text);
            return result;
        }

        size_t start = 0;
        size_t end = text.find(delimiter);

        while (end != std::string::npos) {
            result.push_back(text.substr(start, end - start));
            start = end + delimiter.length();
            end = text.find(delimiter, start);
        }

        result.push_back(text.substr(start));
        return result;
    }

    /**
     * Check if string contains any of the given keywords.
     */
    inline bool ContainsAny(const std::string& str, std::initializer_list<const char*> keywords) {
        for (auto* kw : keywords) {
            if (str.find(kw) != std::string::npos) return true;
        }
        return false;
    }

    /**
     * Strip leading articles ("the", "a", "an") from a lowercase string.
     */
    inline std::string StripArticles(const std::string& text) {
        static const std::vector<std::pair<std::string, size_t>> articles = {
            {"the ", 4}, {"a ", 2}, {"an ", 3}
        };
        for (const auto& [art, len] : articles) {
            if (text.length() > len && text.compare(0, len, art) == 0) {
                return text.substr(len);
            }
        }
        return text;
    }

    /**
     * Token-based match score: ratio of matching words between search and candidate.
     * Articles are ignored. Returns 0.0 (no overlap) to 1.0 (all words match).
     * Both inputs should be lowercase.
     */
    inline float TokenMatchScore(const std::string& search, const std::string& candidate) {
        auto searchWords = Split(search, " ");
        auto candWords = Split(candidate, " ");

        int meaningful = 0;
        int matches = 0;
        for (const auto& sw : searchWords) {
            if (sw == "the" || sw == "a" || sw == "an" || sw.empty()) continue;
            meaningful++;
            for (const auto& cw : candWords) {
                if (sw == cw) { matches++; break; }
            }
        }
        if (meaningful == 0) return 0.0f;
        return static_cast<float>(matches) / static_cast<float>(meaningful);
    }

    /**
     * Result of a fuzzy search.
     */
    struct FuzzyResult {
        std::string match;
        int distance = INT_MAX;
        explicit operator bool() const { return distance < INT_MAX; }
    };

    /**
     * Find the best fuzzy match for a search term in a list of candidates.
     * Returns the closest match within maxDistance, or empty result if none.
     *
     * @param searchTerm Lowercase search term
     * @param candidates List of lowercase candidate names
     * @param maxDistance Maximum Levenshtein distance to accept
     */
    inline FuzzyResult FuzzyFind(const std::string& searchTerm,
                                  const std::vector<std::string>& candidates,
                                  int maxDistance) {
        FuzzyResult result;
        auto searchLen = searchTerm.length();
        for (const auto& name : candidates) {
            int dist = LevenshteinDistance(searchTerm, name);
            if (dist == 0) {
                // Perfect match — no need to check remaining candidates
                result.distance = 0;
                result.match = name;
                return result;
            }
            // Reject matches where every character is effectively wrong —
            // prevents "inn" -> "aho" (distance 3, both length 3)
            auto minLen = std::min(searchLen, name.length());
            if (dist >= static_cast<int>(minLen)) continue;

            if (dist < result.distance) {
                result.distance = dist;
                result.match = name;
            }
        }
        if (result.distance > maxDistance) {
            result.match.clear();
            result.distance = INT_MAX;
        }
        return result;
    }

    /**
     * Enhanced fuzzy find with article stripping and token matching.
     * Resolution order:
     * 1. Article-stripped exact match ("western watchtower" == "the western watchtower")
     * 2. Token-based match (word overlap >= minTokenScore)
     * 3. Levenshtein distance (existing character-level fuzzy)
     */
    inline FuzzyResult TokenFuzzyFind(const std::string& searchTerm,
                                       const std::vector<std::string>& candidates,
                                       int maxLevenshtein,
                                       float minTokenScore = 0.6f) {
        std::string stripped = StripArticles(searchTerm);

        // Phase 1: Article-stripped exact match
        for (const auto& name : candidates) {
            if (StripArticles(name) == stripped) {
                return {name, 0};
            }
        }

        // Phase 2: Token-based match (word overlap)
        FuzzyResult bestToken;
        float bestScore = 0.0f;
        for (const auto& name : candidates) {
            float score = TokenMatchScore(stripped, StripArticles(name));
            if (score >= minTokenScore && score > bestScore) {
                bestScore = score;
                bestToken.match = name;
                bestToken.distance = static_cast<int>((1.0f - score) * 10);
            }
        }
        if (!bestToken.match.empty()) return bestToken;

        // Phase 3: Levenshtein (existing behavior)
        return FuzzyFind(searchTerm, candidates, maxLevenshtein);
    }

}  // namespace IntelEngine::StringUtils
