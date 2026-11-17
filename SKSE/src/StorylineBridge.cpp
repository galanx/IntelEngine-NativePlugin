#include "StorylineBridge.h"

#include <cmath>

namespace IntelEngine {

    namespace {
        std::string NameOf(RE::FormID id) {
            if (id == 0) return {};
            auto* form = RE::TESForm::LookupByID(id);
            if (!form) return {};
            if (auto* ref = form->As<RE::TESObjectREFR>()) {
                const char* name = ref->GetDisplayFullName();
                return name ? name : "";
            }
            const char* name = form->GetName();
            return name ? name : "";
        }

        std::string PlayerName() {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const char* name = player ? player->GetDisplayFullName() : nullptr;
            return name && *name ? name : "the traveller";
        }

        std::string Fill(std::string text, const Storylines::Storyline& s) {
            const std::pair<const char*, std::string> parts[] = {
                {"{player}", PlayerName()}, {"{other}", NameOf(s.other)}, {"{place}", NameOf(s.place)},
                {"{item}", NameOf(s.item)}};
            for (const auto& [key, value] : parts) {
                for (auto at = text.find(key); at != std::string::npos; at = text.find(key, at + value.size())) {
                    text.replace(at, std::strlen(key), value.empty() ? "someone" : value);
                }
            }
            return text;
        }

        std::string Ago(float days) {
            if (days < 0.05f) return "just now";
            if (days < 0.5f) return "earlier today";
            if (days < 1.5f) return "a day ago";
            if (days < 4.0f) return "a few days ago";
            if (days < 10.0f) return "about a week ago";
            return "some time ago";
        }
    }

    float StorylineBridge::NowDays() {
        auto* calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetCurrentGameTime() : 0.0f;
    }

    std::uint32_t StorylineBridge::Open(Storylines::Storyline storyline, bool urgent) {
        std::scoped_lock lock(m_mutex);
        const auto refusal = m_book.Refusal(storyline.kind, storyline.request, storyline.owner, urgent);
        if (!refusal.empty()) {
            logger::info("Storylines: not opened for {:08X}: {}", storyline.owner, refusal);
            return 0;
        }
        const auto id = m_book.Open(storyline, NowDays(), urgent);
        if (id) {
            const auto* s = m_book.Find(id);
            logger::info("Storylines: #{} opened (kind {}, request {}) for {:08X}, slot {}, until day {:.2f}; other {:08X}, place {:08X}, "
                         "target {:08X}, item {:08X} '{}'", id, static_cast<int>(s->kind), static_cast<int>(s->request), s->owner,
                         static_cast<int>(s->slot), s->deadline, s->other, s->place, s->target, s->item, NameOf(s->item));
        }
        return id;
    }

    std::string StorylineBridge::Refusal(Storylines::Kind kind, Storylines::Request request, RE::FormID owner, bool urgent) const {
        std::scoped_lock lock(m_mutex);
        return m_book.Refusal(kind, request, owner, urgent);
    }

    bool StorylineBridge::Apply(std::uint32_t id, Storylines::Event event) {
        std::scoped_lock lock(m_mutex);
        const bool changed = m_book.Apply(id, event, NowDays());
        if (changed) {
            logger::info("Storylines: #{} is now at stage {} (event {})", id, static_cast<int>(m_book.Find(id)->stage),
                         static_cast<int>(event));
        }
        return changed;
    }

    std::vector<std::uint32_t> StorylineBridge::OpenIds() const {
        std::scoped_lock lock(m_mutex);
        std::vector<std::uint32_t> ids;
        for (const auto& s : m_book.all) {
            if (Storylines::IsOpen(s)) ids.push_back(s.id);
        }
        return ids;
    }

    std::vector<std::uint32_t> StorylineBridge::Overdue() const {
        std::scoped_lock lock(m_mutex);
        return m_book.Overdue(NowDays());
    }

    std::optional<Storylines::Storyline> StorylineBridge::Get(std::uint32_t id) const {
        std::scoped_lock lock(m_mutex);
        if (const auto* s = m_book.Find(id)) return *s;
        return std::nullopt;
    }

    std::uint32_t StorylineBridge::OpenIdFor(RE::FormID owner) const {
        std::scoped_lock lock(m_mutex);
        const auto* s = m_book.OpenFor(owner);
        return s ? s->id : 0;
    }

    bool StorylineBridge::Unresolved(RE::FormID owner) const {
        std::scoped_lock lock(m_mutex);
        return m_book.Unresolved(owner);
    }

    void StorylineBridge::MarkFollowedUp(RE::FormID owner) {
        std::scoped_lock lock(m_mutex);
        m_book.MarkFollowedUp(owner);
    }

    std::string StorylineBridge::DescribeFor(RE::Actor* actor) const {
        if (!actor) return {};
        const auto id = actor->GetFormID();
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        std::string out;
        for (const auto& s : m_book.all) {
            if (s.owner != id || s.kind != Storylines::Kind::Request) continue;
            std::string line;
            if (Storylines::IsOpen(s)) {
                const auto memory = [&] {
                    Storylines::Storyline asked = s;
                    asked.stage = Storylines::Stage::Done;  // reuse the phrasing of the request itself
                    auto text = Storylines::OwnerMemory(asked);
                    const auto cut = text.find(", and ");
                    return cut == std::string::npos ? text : text.substr(0, cut);
                }();
                line = Fill(memory, s) + " (" + Ago(now - s.created) + "). ";
                line += s.stage == Storylines::Stage::ReturnTo ? Fill("{player} has done it and owes me a visit to say so.", s)
                                                               : "It is not done yet.";
            } else if (now - s.stageSince < 30.0f) {
                line = Fill(Storylines::OwnerMemory(s), s) + " (" + Ago(now - s.stageSince) + ")";
            }
            if (!line.empty()) out += "- " + line + "\n";
        }
        if (out.empty()) return {};
        return "## What I asked of " + PlayerName() + "\n" + out;
    }

    std::string StorylineBridge::BusinessLine(RE::FormID owner) const {
        const float now = NowDays();
        std::scoped_lock lock(m_mutex);
        const Storylines::Storyline* pick = nullptr;
        for (const auto& s : m_book.all) {
            if (s.owner != owner) continue;
            if (Storylines::IsOpen(s)) {
                pick = &s;
                break;
            }
            if (Storylines::IsUnhappy(s) && !s.followedUp && (!pick || s.stageSince > pick->stageSince)) pick = &s;
        }
        if (!pick) return {};
        std::string text;
        if (Storylines::IsOpen(*pick)) {
            Storylines::Storyline asked = *pick;
            asked.stage = Storylines::Stage::Done;
            text = Storylines::OwnerMemory(asked);
            const auto cut = text.find(", and ");
            if (cut != std::string::npos) text = text.substr(0, cut);
            if (!text.empty()) {
                text += pick->stage == Storylines::Stage::ReturnTo ? "; {player} has done it but not come back yet"
                                                                 : "; still open";
            }
        } else {
            text = Storylines::OwnerMemory(*pick);
        }
        if (text.empty()) return {};
        // First person to third: the line is about the candidate.
        const std::pair<const char*, const char*> swaps[] = {{"I asked ", "asked "}, {"I waited ", "waited "},
                                                            {"I met ", "met "},     {"bring me ", "bring them "},
                                                            {"take me ", "take them "}, {"for me", "for them"},
                                                            {"meet me ", "meet them "}};
        for (const auto& [from, to] : swaps) {
            const std::string f = from, t = to;
            for (auto at = text.find(f); at != std::string::npos; at = text.find(f, at + t.size())) text.replace(at, f.size(), t);
        }
        return Fill(text, *pick) + " (" + Ago(now - (Storylines::IsOpen(*pick) ? pick->created : pick->stageSince)) + ")";
    }

    std::string StorylineBridge::MemoryFor(std::uint32_t id) const {
        std::scoped_lock lock(m_mutex);
        const auto* s = m_book.Find(id);
        if (!s) return {};
        const auto text = Storylines::OwnerMemory(*s);
        return text.empty() ? text : Fill(text, *s);
    }

    void StorylineBridge::Save(SKSE::SerializationInterface* intfc) {
        std::vector<std::uint8_t> bytes;
        {
            std::scoped_lock lock(m_mutex);
            bytes = Storylines::Serialize(m_book);
        }
        if (!intfc->OpenRecord(Storylines::kRecordType, Storylines::kRecordVersion)) {
            logger::error("Storylines: could not open the co-save record");
            return;
        }
        intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }

    void StorylineBridge::Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version != Storylines::kRecordVersion || length > (1u << 20)) {
            logger::warn("Storylines: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("Storylines: the co-save record is short; starting without storylines");
            return;
        }
        Storylines::Book book;
        if (!Storylines::Deserialize(bytes, book)) {
            logger::warn("Storylines: the co-save record is damaged; starting without storylines");
            return;
        }
        const auto resolve = [intfc](std::uint32_t& formID) {
            if (formID == 0) return true;
            RE::FormID resolved = 0;
            if (!intfc->ResolveFormID(formID, resolved)) {
                formID = 0;
                return false;
            }
            formID = resolved;
            return true;
        };
        std::erase_if(book.all, [&](Storylines::Storyline& s) {
            if (!resolve(s.owner) || s.owner == 0) return true;  // the one who asked is gone from the load order
            resolve(s.other);
            resolve(s.place);
            resolve(s.target);
            resolve(s.item);
            return false;
        });
        std::scoped_lock lock(m_mutex);
        m_book = std::move(book);
        logger::info("Storylines: loaded {} ({} open)", m_book.all.size(), m_book.OpenCount());
    }

    void StorylineBridge::Revert() {
        std::scoped_lock lock(m_mutex);
        m_book = Storylines::Book{};
    }
}
