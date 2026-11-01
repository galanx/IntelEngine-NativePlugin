#include "DirectorActorResolution.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using IntelEngine::ResolveDirectorActorFormID;
    using Rejection = IntelEngine::DirectorActorRejection;
    using Entries = std::vector<std::pair<std::uint32_t, std::string>>;
    const auto live = [](std::uint32_t) { return Rejection::None; };
    Entries names{{0xFE000801, "Camilla Valerius"}, {0x4E013BF1, "Brey [Adventurer]"}};
    auto match = ResolveDirectorActorFormID("CAMILLA VALERIUS", names, live);
    Require(match.formId == 0xFE000801 && match.clashId == 0, "unique case-insensitive full name");
    names.emplace_back(0xFE000801, "camilla valerius");
    match = ResolveDirectorActorFormID("Camilla Valerius", names, live);
    Require(match.formId == 0xFE000801 && match.clashId == 0, "same-reference duplicates are not ambiguity");
    names.emplace_back(0xFF000001, "Camilla Valerius");
    match = ResolveDirectorActorFormID("Camilla Valerius", names, live);
    Require(match.formId == 0xFE000801 && match.clashId == 0xFF000001, "two live distinct references must clash");
    for (const auto query : {"Unknown", "Camilla", " Camilla Valerius", "Brey", "Brey  [Adventurer]", ""}) {
        match = ResolveDirectorActorFormID(query, names, live);
        Require(match.formId == 0 && match.clashId == 0 && match.rejection == Rejection::NotFound,
            "no partial/normalized/fuzzy fallback");
    }
    Require(ResolveDirectorActorFormID("Brey [Adventurer]", names, live).formId == 0x4E013BF1,
        "role suffix is part of the exact full name");
    Require(ResolveDirectorActorFormID("Nobody", Entries{{0, "Nobody"}}, live).formId == 0, "zero is invalid");
    for (const auto reason : {Rejection::Disabled, Rejection::Dead, Rejection::Deleted,
                             Rejection::LookupNull, Rejection::IdMismatch}) {
        for (const auto& entries : {Entries{{1, "Camilla"}, {2, "Camilla"}},
                                   Entries{{2, "Camilla"}, {1, "Camilla"}}}) {
            match = ResolveDirectorActorFormID("Camilla", entries, [reason](std::uint32_t id) {
                return id == 1 ? reason : Rejection::None;
            });
            Require(match.formId == 2 && match.clashId == 0 && match.rejection == Rejection::None,
                "ineligible duplicate must not hide a unique live actor");
        }
    }
    match = ResolveDirectorActorFormID("Camilla", Entries{{1, "Camilla"}},
        [](std::uint32_t) { return Rejection::Disabled; });
    Require(match.formId == 0 && match.clashId == 0 && match.rejectedFormId == 1 &&
        std::string_view(IntelEngine::DirectorActorRejectionName(match.rejection)) == "disabled",
        "only disabled must reject with disabled reason");
    for (const auto& entries : {Entries{{1, "Camilla"}, {2, "Camilla"}},
                               Entries{{2, "Camilla"}, {1, "Camilla"}}}) {
        match = ResolveDirectorActorFormID("Camilla", entries, [](std::uint32_t id) {
            return id == 1 ? Rejection::LookupNull : Rejection::Dead;
        });
        Require(match.formId == 0 && match.rejectedFormId == 2 && match.rejection == Rejection::Dead,
            "all-ineligible diagnostic must prefer specific state over missing lookup, independent of order");
    }
    std::cout << "PASS: manual unique, duplicate identity, eligible-only ambiguity, disabled/dead exclusion, specific rejection and strict full-name contract\n";
}
