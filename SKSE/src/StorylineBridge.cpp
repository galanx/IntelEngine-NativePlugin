#include "StorylineBridge.h"

#include "LivingWorldBridge.h"
#include "SkyrimNetAPI.h"

#include <cmath>
#include <thread>

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
                {"{item}", NameOf(s.item)},  {"{owner}", NameOf(s.owner)}, {"{before}", NameOf(s.before)}};
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

        // How long is left until a deadline, in words; empty when none or past.
        std::string Left(float days) {
            if (days <= 0.0f) return {};
            if (days < 0.3f) return "within a few hours";
            if (days < 0.75f) return "within half a day";
            if (days < 1.5f) return "within a day";
            return "within " + std::to_string(static_cast<int>(std::lround(days))) + " days";
        }

        // What was asked, first person, without how it ended ("I asked {player} to find {other}").
        std::string Asked(const Storylines::Storyline& s) {
            Storylines::Storyline asked = s;
            asked.stage = Storylines::Stage::Done;
            asked.reply.clear();
            asked.changes = static_cast<std::uint8_t>(asked.changes & ~Storylines::Changed::ComesAlong);
            auto text = Storylines::OwnerMemory(asked);
            const auto cut = text.find(", and ");
            return cut == std::string::npos ? text : text.substr(0, cut);
        }

        // The player is right here with this NPC (same space, a few steps away).
        bool PlayerIsWith(RE::Actor* actor) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !actor || !actor->Is3DLoaded()) return false;
            auto* cell = actor->GetParentCell();
            auto* playerCell = player->GetParentCell();
            if (!cell || !playerCell) return false;
            if (cell->IsInteriorCell() || playerCell->IsInteriorCell()) {
                if (cell != playerCell) return false;
            } else if (actor->GetWorldspace() != player->GetWorldspace()) {
                return false;
            }
            return actor->GetPosition().GetDistance(player->GetPosition()) < 1500.0f;
        }
    }

    float StorylineBridge::NowDays() {
        auto* calendar = RE::Calendar::GetSingleton();
        return calendar ? calendar->GetCurrentGameTime() : 0.0f;
    }

    std::uint32_t StorylineBridge::Open(Storylines::Storyline storyline, bool urgent) {
        // The reward is fixed when it is asked (the player's level now), so what the asker says of it stays true.
        if (storyline.kind == Storylines::Kind::Request && storyline.promised == 0) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            storyline.promised = static_cast<std::uint32_t>(Storylines::OfferedGold(storyline, player ? player->GetLevel() : 1));
        }
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
        std::string deed;
        std::string hold;
        std::string owner;
        {
            std::scoped_lock lock(m_mutex);
            const bool changed = m_book.Apply(id, event, NowDays());
            if (!changed) return false;
            const auto* s = m_book.Find(id);
            logger::info("Storylines: #{} is now at stage {} (event {})", id, static_cast<int>(s->stage), static_cast<int>(event));
            // 4.0 consequences: how a request ended is heard of in the asker's hold. People bring it up when the talk
            // is about the player or the asker (semantic retrieval only), so it costs nothing in other prompts.
            const auto text = Storylines::PublicDeed(*s);
            if (!text.empty()) {
                owner = NameOf(s->owner);
                auto* ownerActor = s->owner ? RE::TESForm::LookupByID<RE::Actor>(s->owner) : nullptr;
                hold = LivingWorldBridge::HoldName(ownerActor);
                deed = Fill(text, *s);
                for (auto at = deed.find("{owner}"); at != std::string::npos; at = deed.find("{owner}", at + owner.size())) {
                    deed.replace(at, 7, owner.empty() ? "someone" : owner);
                }
                auto* base = ownerActor ? ownerActor->GetActorBase() : nullptr;
                const std::string them = !base ? "them" : base->GetSex() == RE::SEX::kFemale ? "her" : "him";
                for (auto at = deed.find("{them}"); at != std::string::npos; at = deed.find("{them}", at + them.size())) {
                    deed.replace(at, 6, them);
                }
            }
        }
        if (!deed.empty() && !hold.empty() && SkyrimNetAPI::AddWorldKnowledge) {
            auto escape = [](const std::string& text) {
                std::string out;
                for (const char c : text) {
                    if (c == '"' || c == '\\') out += '\\';
                    out += c;
                }
                return out;
            };
            const std::string condition = "get_intelengine_hold(actorUUID) == \"" + escape(hold) + "\" and actorName != \"" +
                                          escape(owner) + "\"";
            const std::string label = "IntelEngine deed: " + owner;
            std::thread([deed, condition, label]() {
                const int knowledge = SkyrimNetAPI::AddWorldKnowledge(deed.c_str(), condition.c_str(), false, 0.45f, label.c_str());
                logger::info("Storylines: heard in the hold ({}): {}", knowledge, deed);
            }).detach();
        }
        return true;
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
        auto* player = RE::PlayerCharacter::GetSingleton();
        const int level = player ? player->GetLevel() : 1;
        const bool withPlayer = PlayerIsWith(actor);
        namespace SL = Storylines;
        std::scoped_lock lock(m_mutex);
        std::string mine;
        std::string aboutMe;
        for (const auto& s : m_book.all) {
            if (s.kind != SL::Kind::Request) continue;
            if (s.owner == id) {
                std::string line;
                if (SL::IsOpen(s)) {
                    line = Fill(Asked(s), s) + " (" + Ago(now - s.created) + "). ";
                    line += s.stage == SL::Stage::ReturnTo ? Fill("{player} has done it and owes me a visit to say so.", s)
                                                           : "It is not done yet.";
                    if (const auto left = s.deadline > 0.0f ? Left(s.deadline - now) : std::string(); !left.empty()) {
                        line += (s.changes & SL::Changed::Hurried) ? " It has become urgent: I need it " + left + "."
                                                                   : " I need it " + left + ".";
                    }
                    // The reward is fixed, so what the asker says of it is what is paid.
                    line += " I offered " + std::to_string(SL::OfferedGold(s, level)) + " gold for it" +
                            ((s.changes & SL::Changed::Raised) ? ", more than at first." : ".");
                    if (!s.note.empty()) line += " Why I changed it: " + s.note + ".";
                } else if (now - s.stageSince < 30.0f) {
                    line = Fill(SL::OwnerMemory(s), s) + " (" + Ago(now - s.stageSince) + ")";
                }
                if (!line.empty()) mine += "- " + line + "\n";
            } else if (s.other == id && (s.request == SL::Request::FindPerson || s.request == SL::Request::DeliverWord)) {
                // The one it is about: what they answered, or (while the player is with them) that they may answer.
                std::string line;
                if (const auto memory = SL::OtherMemory(s); !memory.empty()) {
                    if (SL::IsOpen(s) || now - s.stageSince < 30.0f) line = Fill(memory, s) + " (" + Ago(now - s.stageSince) + ")";
                } else if (SL::IsOpen(s) && (s.stage == SL::Stage::ReturnTo || withPlayer)) {
                    line = s.request == SL::Request::FindPerson ? "{owner} asked {player} to find me (" : "{owner} asked {player} to speak with me (";
                    line += Ago(now - s.created) + ")";
                    line += s.stage == SL::Stage::ReturnTo && !withPlayer ? ", and {player} did." : "; {player} is here now.";
                    if (withPlayer) {
                        line += " I know of it only once {player} says so. I may send {owner} an answer through {player}, or go "
                                "along with {player} to see {owner}.";
                    }
                    line = Fill(line, s);
                }
                if (!line.empty()) aboutMe += "- " + line + "\n";
            }
        }
        std::string out;
        if (!mine.empty()) out += "## What I asked of " + PlayerName() + "\n" + mine;
        if (!aboutMe.empty()) out += "## Errands about me\n" + aboutMe;
        return out;
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

    bool StorylineBridge::CallOff(std::uint32_t id, const std::string& note) {
        std::scoped_lock lock(m_mutex);
        const bool done = m_book.CallOff(id, note, NowDays());
        logger::info("Storylines: #{} {} ({})", id, done ? "called off by the one who asked" : "could not be called off", note);
        return done;
    }

    std::string StorylineBridge::Amend(std::uint32_t id, Storylines::Book::Amendment amendment) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        const int level = player ? player->GetLevel() : 1;
        std::scoped_lock lock(m_mutex);
        if (const auto* s = m_book.Find(id)) amendment.offered = Storylines::OfferedGold(*s, level);
        auto why = m_book.Amend(id, amendment, NowDays());
        if (why.empty()) {
            const auto* s = m_book.Find(id);
            logger::info("Storylines: #{} changed ({}): until day {:.2f}, {} gold, other {:08X}, item {:08X}, target {:08X}; {}", id,
                         static_cast<int>(amendment.change), s->deadline, Storylines::OfferedGold(*s, level), s->other, s->item,
                         s->target, s->note);
        } else {
            logger::info("Storylines: #{} not changed ({}): {}", id, static_cast<int>(amendment.change), why);
        }
        return why;
    }

    std::string StorylineBridge::Reply(std::uint32_t id, RE::FormID speaker, const std::string& text, bool comesAlong) {
        std::scoped_lock lock(m_mutex);
        auto why = m_book.Reply(id, speaker, text, comesAlong, NowDays());
        logger::info("Storylines: #{} answer from {:08X}{}: {}", id, speaker, comesAlong ? " (comes along)" : "",
                     why.empty() ? text : "refused, " + why);
        return why;
    }

    std::uint32_t StorylineBridge::AboutId(RE::FormID person) const {
        std::scoped_lock lock(m_mutex);
        return m_book.AboutId(person);
    }

    std::string StorylineBridge::AnswerPending(RE::Actor* actor) const {
        if (!actor || !PlayerIsWith(actor)) return {};
        std::scoped_lock lock(m_mutex);
        const auto id = m_book.AboutId(actor->GetFormID());
        const auto* s = id ? m_book.Find(id) : nullptr;
        if (!s || !s->reply.empty() || (s->changes & Storylines::Changed::ComesAlong)) return {};
        return std::to_string(id);
    }

    std::string StorylineBridge::OtherMemoryFor(std::uint32_t id) const {
        std::scoped_lock lock(m_mutex);
        const auto* s = m_book.Find(id);
        if (!s) return {};
        const auto text = Storylines::OtherMemory(*s);
        return text.empty() ? text : Fill(text, *s);
    }

    int StorylineBridge::OfferedGold(std::uint32_t id, int playerLevel) const {
        std::scoped_lock lock(m_mutex);
        const auto* s = m_book.Find(id);
        return s ? Storylines::OfferedGold(*s, playerLevel) : 0;
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
        if (version < 1 || version > Storylines::kRecordVersion || length > (1u << 20)) {
            logger::warn("Storylines: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("Storylines: the co-save record is short; starting without storylines");
            return;
        }
        Storylines::Book book;
        if (!Storylines::Deserialize(bytes, book, version)) {
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
            resolve(s.before);
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
