#include "LanguageBridge.h"
#include "GameText.h"
#include "LanguagePack.h"
#include "PlaceWords.h"
#include "SkyrimNetAPI.h"
#include "StringUtils.h"
#include "Utf8Fold.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace IntelEngine {

    namespace L = Language;

    // What is in force, built once per decision and handed out whole.
    struct LanguageBridge::State {
        std::shared_ptr<const std::vector<L::Pack>> packs;   // keeps the packs alive that the pointers below go into
        const L::Pack* text = nullptr;                       // what the player reads; none: English
        std::vector<const L::Pack*> spoken;
        std::vector<const std::vector<L::Word>*> time, place, none;
        PlaceWords::Packs places;                            // the packs in force and English, for words of place
        std::vector<std::u32string> free;                    // sorted
        std::string line;                                    // for the log
    };

    namespace {
        constexpr const char* kFolder = "Data/SKSE/Plugins/IntelEngine/languages";
        constexpr const char* kTranslator = "Data/SKSE/Plugins/SkyrimNet/config/UniversalTranslator.yaml";

        std::string FileText(const std::filesystem::path& path) {
            std::ifstream in(path, std::ios::binary);
            if (!in) return {};
            std::stringstream text;
            text << in.rdbuf();
            return text.str();
        }

        // Skyrim.ini's sLanguage ("ENGLISH", "RUSSIAN").
        std::string GameWord() {
            auto* ini = RE::INISettingCollection::GetSingleton();
            auto* setting = ini ? ini->GetSetting("sLanguage:General") : nullptr;
            if (setting && setting->GetType() == RE::Setting::Type::kString) {
                if (const char* word = setting->GetString()) return word;
            }
            return {};
        }

        std::string ConfigValue(const char* path) {
            if (!SkyrimNetAPI::GetPluginConfigValue) return "auto";
            std::string value = SkyrimNetAPI::GetPluginConfigValue("IntelEngine", path, "auto");
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
            return value.empty() ? "auto" : value;
        }

        // A page the tables in GameText.h do not have (the double-byte ones), read and written by Windows.
        std::string SystemPage(std::string_view text, int page, bool toUtf8) {
            if (text.empty()) return {};
            const UINT from = toUtf8 ? static_cast<UINT>(page) : CP_UTF8;
            const UINT to = toUtf8 ? CP_UTF8 : static_cast<UINT>(page);
            const int wideLength = MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (wideLength <= 0) return std::string(text);
            std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
            MultiByteToWideChar(from, 0, text.data(), static_cast<int>(text.size()), wide.data(), wideLength);
            const int length = WideCharToMultiByte(to, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
            if (length <= 0) return std::string(text);
            std::string out(static_cast<std::size_t>(length), '\0');
            WideCharToMultiByte(to, 0, wide.data(), wideLength, out.data(), length, nullptr, nullptr);
            return out;
        }

        // The game's own place names as it hands them out, for the script they are written in. Read here, before
        // anything else reads a name, so that the page of old text is decided by then.
        std::vector<std::string> GameLocationNames() {
            std::vector<std::string> names;
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) return names;
            for (auto* location : data->GetFormArray<RE::BGSLocation>()) {
                const char* name = location ? location->GetFullName() : nullptr;
                if (name && *name && GameText::WellFormed(name)) names.emplace_back(name);
            }
            return names;
        }

        std::vector<std::string> Commas(const std::string& list) {
            std::vector<std::string> out;
            std::stringstream in(list);
            for (std::string piece; std::getline(in, piece, ',');) {
                const auto b = piece.find_first_not_of(" \t\"");
                if (b == std::string::npos) continue;
                const auto e = piece.find_last_not_of(" \t\"");
                out.push_back(piece.substr(b, e - b + 1));
            }
            return out;
        }
    }

    LanguageBridge* LanguageBridge::GetSingleton() {
        static LanguageBridge instance;
        return &instance;
    }

    std::shared_ptr<const LanguageBridge::State> LanguageBridge::Now() const {
        std::lock_guard lock(m_mutex);
        return m_state;
    }

    void LanguageBridge::Load() {
        const auto began = std::chrono::steady_clock::now();
        auto packs = std::make_shared<std::vector<L::Pack>>();
        std::error_code error;
        try {
            for (const auto& file : std::filesystem::directory_iterator(kFolder, error)) {
                if (file.path().extension() != ".tsv") continue;
                const std::string stem = file.path().stem().string();
                if (stem.empty() || stem.front() == '_') continue;   // a template for translators
                L::Pack pack = L::ReadPack(FileText(file.path()), stem, [](const std::string& s) { return Folded(s); });
                logger::info("Language pack {}: {} ({}), {} rows, {} labels{}", pack.file, pack.name, pack.code, pack.rows, pack.labels.size(),
                             pack.bad ? fmt::format(", {} line(s) NOT understood", pack.bad) : std::string{});
                if (pack.code.empty() || pack.name.empty()) {
                    logger::warn("Language pack {}: no meta code or name; left out", pack.file);
                    continue;
                }
                packs->push_back(std::move(pack));
            }
        } catch (const std::exception& e) {
            logger::warn("Language packs: {}", e.what());
        }
        if (error) logger::info("No language packs ({})", error.message());
        std::sort(packs->begin(), packs->end(), [](const L::Pack& a, const L::Pack& b) { return a.file < b.file; });
        {
            std::lock_guard lock(m_mutex);
            m_packs = std::move(packs);
            m_state.reset();
        }
        logger::info("Language packs read in {:.1f} ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count());
        GameText::OtherPages().store(&SystemPage);
        // The script of the game's own place names, now: the page of old text depends on the language, and every
        // index that reads names is built after this.
        {
            L::ScriptCount count;
            for (const auto& name : GameLocationNames()) count.Add(name);
            std::lock_guard lock(m_mutex);
            m_namesScript = count.Script();
        }
        Decide();
    }

    std::string LanguageBridge::Folded(const std::string& said) { return LetterFold::Plain(FoldUtf8(GameText::In(said))); }

    void LanguageBridge::NoteNames(const std::vector<std::string>& names) {
        L::ScriptCount count;
        for (const auto& name : names) count.Add(name);
        {
            std::lock_guard lock(m_mutex);
            m_namesScript = count.Script();
            m_namesNoted = true;
        }
        logger::info("Language: the game's own place names are written in {} ({} names: {} Latin, {} Cyrillic, {} Hangul, {} kana, {} Han letters)",
                     count.Script(), names.size(), count.latin, count.cyrillic, count.hangul, count.kana, count.han);
        Decide();
    }

    void LanguageBridge::Decide() {
        std::shared_ptr<const std::vector<L::Pack>> packs;
        std::string script;
        {
            std::lock_guard lock(m_mutex);
            packs = m_packs;
            script = m_namesScript;
        }
        if (!packs) return;

        const std::string gameWord = GameWord();
        const std::string wantText = ConfigValue("language.text");
        const std::string wantSpoken = ConfigValue("language.spoken");

        auto state = std::make_shared<State>();
        state->packs = packs;
        state->text = L::TextPack(*packs, wantText, gameWord, script);

        std::vector<std::string> codes;
        std::string from;
        const std::string spokenLower = StringUtils::ToLowerStd(wantSpoken);
        if (spokenLower == "auto") {
            codes = L::SpokenCodesOfTranslator(FileText(kTranslator));
            if (!codes.empty()) from = " (from SkyrimNet's translator)";
        } else {
            codes = Commas(wantSpoken);
        }
        state->spoken = L::SpokenPacks(*packs, codes);

        std::vector<const L::Pack*> all;
        if (state->text) all.push_back(state->text);
        for (const auto* p : state->spoken) {
            if (std::find(all.begin(), all.end(), p) == all.end()) all.push_back(p);
        }
        for (const auto* p : all) {
            state->time.push_back(&p->time);
            state->place.push_back(&p->place);
            state->none.push_back(&p->none);
            state->places.push_back(p);
            for (const auto& w : p->free) state->free.push_back(w);
        }
        state->places.push_back(&PlaceWords::English());

        // Text that is no UTF-8 (GameText.h): the page the user names, else the page of the language the player reads
        // (a pack may say it: meta codepage), else of the game's own language; Western where nothing tells.
        const int namedPage = GameText::PageFromSetting(ConfigValue("language.codepage"));
        int page = namedPage;
        const char* why = "named in the settings";
        if (page <= 0 || page == GameText::kUtf8) {
            page = state->text ? (state->text->codepage > 0 ? state->text->codepage : GameText::PageOfLanguage(state->text->code)) : 0;
            why = "the language the player reads";
            if (page <= 0) {
                page = GameText::PageOfLanguage(gameWord);
                why = "the game's language";
            }
            if (page <= 0) {
                page = 1252;
                why = "nothing tells: Western";
            }
        }
        const int shown = GameText::PageFromSetting(ConfigValue("language.display"));
        GameText::Use(page, namedPage <= 0 || namedPage == GameText::kUtf8, shown > 0 ? shown : GameText::kUtf8);
        for (const char32_t* article : {U"the", U"a", U"an"}) state->free.emplace_back(article);
        std::sort(state->free.begin(), state->free.end());
        state->free.erase(std::unique(state->free.begin(), state->free.end()), state->free.end());

        std::string spokenNames;
        for (const auto* p : state->spoken) spokenNames += (spokenNames.empty() ? "" : ", ") + p->name;
        std::string unknown;
        for (const auto& c : codes) {
            const std::string lower = StringUtils::ToLowerStd(c);
            if (lower == "auto" || lower == "game" || lower == "en" || lower == "english") continue;
            bool known = false;
            for (const auto& p : *packs) known = known || p.Is(lower);
            if (!known) unknown += (unknown.empty() ? "" : ", ") + c;
        }
        state->line = fmt::format("the game says {}, its place names are written in {}; the player reads {}{}; people speak {}{}{}; "
                                  "text that is no UTF-8 is read as code page {} ({}){}",
                                  gameWord.empty() ? "nothing" : gameWord, script, state->text ? state->text->name : "English",
                                  StringUtils::ToLowerStd(wantText) == "auto" ? "" : " (chosen)",
                                  spokenNames.empty() ? "what the game is in" : spokenNames, from,
                                  unknown.empty() ? "" : fmt::format("; no pack for: {}", unknown), page, why,
                                  shown > 0 && shown != GameText::kUtf8 ? fmt::format(", the game is shown code page {}", shown) : std::string{});

        bool changed = false;
        {
            std::lock_guard lock(m_mutex);
            changed = !m_state || m_state->line != state->line;
            m_state = state;
        }
        if (changed) logger::info("Language: {}", state->line);
    }

    std::string LanguageBridge::TimeToEnglish(const std::string& said) const {
        const auto state = Now();
        if (!state || state->time.empty()) return said;
        return L::TimeToEnglish(Folded(said), state->time);
    }

    std::string LanguageBridge::PlaceToEnglish(const std::string& said) const {
        const auto state = Now();
        if (!state || state->place.empty()) return {};
        return L::PlaceToEnglish(Folded(said), state->place, Free());
    }

    namespace {
        const PlaceWords::Packs& EnglishAlone() {
            static const PlaceWords::Packs packs{&PlaceWords::English()};
            return packs;
        }
    }

    PlaceWords::Intent LanguageBridge::ReadPlace(const std::string& said) const {
        const auto state = Now();
        return PlaceWords::Read(Folded(said), state ? state->places : EnglishAlone(), Free());
    }

    bool LanguageBridge::IsPlaceWord(const std::string& said) const {
        const auto state = Now();
        return PlaceWords::Plain(Folded(said), state ? state->places : EnglishAlone(), Free());
    }

    bool LanguageBridge::NameSays(const std::string& name, std::string_view what, bool englishOnly) const {
        const auto state = Now();
        return PlaceWords::NameSays(Folded(name), what, state && !englishOnly ? state->places : EnglishAlone());
    }

    std::string LanguageBridge::PlaceBehind(const std::string& said) const {
        const auto state = Now();
        return PlaceWords::Behind(Folded(said), state ? state->places : EnglishAlone());
    }

    bool LanguageBridge::IsNone(const std::string& said) const {
        const auto state = Now();
        static const std::vector<const std::vector<L::Word>*> kNoLists;
        return L::IsNone(Folded(said), state ? state->none : kNoLists);
    }

    NameForms::FreeWord LanguageBridge::Free() const {
        auto state = Now();
        if (!state) {
            return [](std::u32string_view w) { return w == U"the" || w == U"a" || w == U"an"; };
        }
        return [state](std::u32string_view w) {
            return std::binary_search(state->free.begin(), state->free.end(), w,
                                      [](const auto& a, const auto& b) { return std::u32string_view(a) < std::u32string_view(b); });
        };
    }

    std::string LanguageBridge::Label(const std::string& english) const {
        const auto state = Now();
        if (!state || !state->text || state->text->labels.empty() || english.empty()) return GameText::Out(english);
        if (auto there = L::Translate(state->text->labels, english)) return GameText::Out(*there);
        return GameText::Out(english);
    }

    std::string LanguageBridge::TextLanguage() const {
        const auto state = Now();
        return state && state->text ? state->text->name : std::string{};
    }

    std::string LanguageBridge::Describe() const {
        const auto state = Now();
        return state ? state->line : std::string{"no language packs read yet"};
    }
}
