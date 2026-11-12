#include "InformantRumorValidation.h"

#include <iostream>
#include <stdexcept>

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using IntelEngine::InformantRumorRejection;
    Require(InformantRumorRejection("Mikael", "Mikael mocked Sinmir's views on the city's rot", "").empty(), "full sentence naming subject passes");
    Require(InformantRumorRejection("Sinmir [the skald]", "Mikael mocked sinmir's views", "").empty(), "tagged subject, case-insensitive name passes");
    Require(InformantRumorRejection("Sinmir", "Sinmir argued with Hulda", "").empty(), "sender==subject style sentence passes");
    Require(InformantRumorRejection("Irving", "Irving argued with Hulda", "").empty(), "subject whose name ends in 'ing' passes");
    Require(InformantRumorRejection("Sinmir", "", "") == "rumor is empty", "empty rumor and gossip rejected");
    Require(InformantRumorRejection("Sinmir", "Mikael mocked his views on the city's rot", "") == "rumor does not name the subject", "pronoun-only rumor rejected");
    Require(InformantRumorRejection("Sinmir", "arguing with Hulda about the town's decay Sinmir", "") == "rumor starts with an -ing word", "gerund start rejected");
    Require(InformantRumorRejection("", "Mikael mocked Sinmir", "") == "subject is empty", "empty subject rejected");
    Require(InformantRumorRejection("Sinmir", "", "was seen near the gate").empty(), "legacy gossip phrase passes");
    Require(InformantRumorRejection("Sinmir", "", "arguing with Hulda") == "rumor starts with an -ing word", "legacy gerund rejected");
    Require(InformantRumorRejection("Ria", "Victoria was robbed.", "") == "rumor does not name the subject", "name inside another word is not naming the subject");
    Require(InformantRumorRejection("Ria", "Ria's purse was stolen.", "").empty(), "possessive names the subject");
    Require(InformantRumorRejection("Ria", "Ria was robbed.", "").empty(), "whole-word name passes");
    Require(InformantRumorRejection("Ria", "Riften guards robbed Ria.", "").empty(), "Riften is not Ria but Ria is named later");
    Require(InformantRumorRejection("Mikael", "Running, Mikael stole the purse.", "") == "rumor starts with an -ing word", "gerund before a comma rejected");
    Require(InformantRumorRejection("Billy [Citizen]", "Billy was robbed near the well.", "").empty(), "tagged subject names itself");
    Require(InformantRumorRejection("J'zargo", "J'zargo's spell burned the inn.", "").empty(), "apostrophe name with possessive passes");
    Require(InformantRumorRejection("Ria", "J'zargo robbed Ria.", "").empty(), "apostrophe name elsewhere does not hide the subject");
    std::cout << "PASS: informant rumor validation (name, -ing start, empty, legacy gossip)\n";
}
