#pragma once

#include "GameText.h"
#include "LetterFold.h"
#include "Utf8Fold.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace IntelEngine {

// Higher values are more specific when every exact-name reference is ineligible.
enum class DirectorActorRejection { None, NotFound, LookupNull, IdMismatch, Deleted, Disabled, Dead };

inline const char* DirectorActorRejectionName(DirectorActorRejection reason) {
    switch (reason) {
        case DirectorActorRejection::None: return "";
        case DirectorActorRejection::NotFound: return "not-found";
        case DirectorActorRejection::LookupNull: return "lookup-null";
        case DirectorActorRejection::IdMismatch: return "id-mismatch";
        case DirectorActorRejection::Deleted: return "deleted";
        case DirectorActorRejection::Disabled: return "disabled";
        case DirectorActorRejection::Dead: return "dead";
    }
    return "not-found";
}

struct DirectorActorMatch {
    std::uint32_t formId = 0;
    std::uint32_t clashId = 0;
    std::uint32_t rejectedFormId = 0;
    DirectorActorRejection rejection = DirectorActorRejection::NotFound;
};

// Entries contain actor-reference IDs, never NPC base IDs. No normalization or fuzzy fallback.
template <typename Entries, typename Eligibility>
DirectorActorMatch ResolveDirectorActorFormID(std::string_view name, const Entries& entries, Eligibility eligibility) {
    DirectorActorMatch result;
    if (name.empty()) return result;
    // lower case in every script, not byte by byte; the same letters with their marks or without ("Elisif" for the
    // name with an acute E); the whole name still
    const std::string wanted = LetterFold::Plain(FoldUtf8(GameText::In(std::string(name))));
    for (const auto& [id, candidate] : entries) {
        if (!id || LetterFold::Plain(FoldUtf8(std::string(candidate))) != wanted) continue;
        const auto reason = eligibility(id);
        if (reason != DirectorActorRejection::None) {
            if (!result.formId && (reason > result.rejection ||
                (reason == result.rejection && id < result.rejectedFormId))) {
                result.rejection = reason;
                result.rejectedFormId = id;
            }
            continue;
        }
        if (result.formId && result.formId != id) {
            result.clashId = id;
            return result;
        }
        result.formId = id;
        result.rejection = DirectorActorRejection::None;
        result.rejectedFormId = 0;
    }
    return result;
}

}  // namespace IntelEngine
