#include "LanguageBridge.h"
#include "LanguagePack.h"
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
                L::Pack pack = L::ReadPack(FileText(file.path()), stem, [](const std::string& s) { return FoldUtf8(s); });
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
        Decide();
    }

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
            for (const auto& w : p->free) state->free.push_back(w);
        }
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
        state->line = fmt::format("the game says {}, its place names are written in {}; the player reads {}{}; people speak {}{}{}",
                                  gameWord.empty() ? "nothing" : gameWord, script, state->text ? state->text->name : "English",
                                  StringUtils::ToLowerStd(wantText) == "auto" ? "" : " (chosen)",
                                  spokenNames.empty() ? "what the game is in" : spokenNames, from,
                                  unknown.empty() ? "" : fmt::format("; no pack for: {}", unknown));

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
        return L::TimeToEnglish(FoldUtf8(said), state->time);
    }

    std::string LanguageBridge::PlaceToEnglish(const std::string& said) const {
        const auto state = Now();
        if (!state || state->place.empty()) return {};
        return L::PlaceToEnglish(FoldUtf8(said), state->place, Free());
    }

    bool LanguageBridge::IsNone(const std::string& said) const {
        const auto state = Now();
        static const std::vector<const std::vector<L::Word>*> kNoLists;
        return L::IsNone(FoldUtf8(said), state ? state->none : kNoLists);
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
        if (!state || !state->text || state->text->labels.empty() || english.empty()) return english;
        if (auto there = L::Translate(state->text->labels, english)) return *there;
        return english;
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
