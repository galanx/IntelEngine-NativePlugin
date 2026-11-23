#pragma once

// The language packs in the game (LanguagePack.h has what a pack is and every rule; this is where the files are
// read and where it is decided which of them are in force).
//
//   - Read once, when the game's data is loaded: Data/SKSE/Plugins/IntelEngine/languages/*.tsv (a name beginning
//     with "_" is a template, not a pack).
//   - Decided at every load of a game and whenever the settings are taken over:
//       what the player reads   "Language of journal and messages" (language.text): auto, or a pack by name. Auto
//                               goes by the game's own setting (Skyrim.ini, sLanguage) and, where that says English,
//                               by the script the game's own place names are written in (a translation the game
//                               never shipped, Korean for one, is played with the setting left at English).
//       what people speak       "Languages people speak" (language.spoken): auto, or names. Auto reads SkyrimNet's
//                               translator settings (UniversalTranslator.yaml: the spoken languages of its rules),
//                               which is how an English game whose people speak Gheg Albanian is told.
//     The words of time, of place and for "none" of the reading pack and of every spoken pack are in force
//     together; labels come from the reading pack alone.
//       text that is no UTF-8   "Code page of old text" (language.codepage): auto, or a Windows code page (1250,
//                               1251, 1252 ...). The game's own text is UTF-8 in every language; names a plugin or
//                               an old translation carries may be in a page of the old game (GameText.h). Auto: the
//                               page of the language the player reads (a pack's meta codepage, else by its code), and
//                               Cyrillic wherever the text itself shows it. "Code page the game shows"
//                               (language.display) is UTF-8 unless a number says otherwise.
//
// Everything a caller asks is answered from a snapshot, without waiting for a lock that a reload could hold.

#include "NameForms.h"
#include "PlaceWords.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace IntelEngine {

    namespace Language {
        struct Pack;
    }

    class LanguageBridge {
    public:
        static LanguageBridge* GetSingleton();

        // The pack files, once.
        void Load();
        // The game's own names of places, before any names from files are added: what script are they written in?
        void NoteNames(const std::vector<std::string>& names);
        // Which packs are in force. Cheap; logs when the answer changed.
        void Decide();

        // A time as the model wrote it, in the English the time reader knows. English comes back as it was.
        std::string TimeToEnglish(const std::string& said) const;
        // The English word of place ("home", "outside") when the whole of what was said is one; empty when it is a
        // name (or English already).
        std::string PlaceToEnglish(const std::string& said) const;
        // Nothing, "none", or the word for it in a language in force.
        bool IsNone(const std::string& said) const;
        // What was said as a word of place ("upstairs", "Alvor's house", "nach draussen"), by the rows of English and
        // of the packs in force (PlaceWords.h); Kind::None for a name.
        PlaceWords::Intent ReadPlace(const std::string& said) const;
        // The whole of what was said is a plain word of place, or of a spot in a room.
        bool IsPlaceWord(const std::string& said) const;
        // The name of a door, a cell or an object says what it is ("upstairs", "downstairs", "bed", "cooking", "bath",
        // "water"). `englishOnly`: for editor ids, which are English in every game.
        bool NameSays(const std::string& name, std::string_view what, bool englishOnly = false) const;
        // The place behind a word of leaving or of going ("out of Helgen", "go to Riverwood"): the place, or empty.
        std::string PlaceBehind(const std::string& said) const;
        // The same folding the packs' words were read with: lower case, plain letters.
        static std::string Folded(const std::string& said);
        // Little words beside a name that are no part of it: the English articles and those of the packs in force.
        NameForms::FreeWord Free() const;
        // What the player reads: the sentence in the reading pack's words when it has them, else as it came.
        std::string Label(const std::string& english) const;
        // The English name of the language the player reads in ("Russian"); empty for English.
        std::string TextLanguage() const;
        // One line for the log and the dashboard.
        std::string Describe() const;

    private:
        struct State;
        std::shared_ptr<const State> Now() const;

        mutable std::mutex m_mutex;
        std::shared_ptr<const std::vector<Language::Pack>> m_packs;
        std::shared_ptr<const State> m_state;
        std::string m_namesScript = "latin";
        bool m_namesNoted = false;
    };
}
