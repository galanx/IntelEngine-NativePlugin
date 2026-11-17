#pragma once

// IntelEngine 4.0 storylines: what someone asked of the player, where, until when, and how it ended. Pure logic
// without game types, checked offline by SKSE/tests/StorylinesCheck.cpp; the scripts show the objectives in the
// "Unfinished Business" quest (IntelEngine_Threads, six slots) and report what happened.
//
// Rules:
//  * Everything shown is real: a storyline is opened only with a person, place or item that exists, and the marker
//    points at it. Every storyline ends: done, declined, ignored (its time ran out) or failed.
//  * An ending is remembered by the people in it (first-person memories the scripts hand to SkyrimNet), and an
//    ignored, declined or failed storyline leaves unfinished business the director can come back to.
//  * Variety: at most six open at once, never two from the same person, and not the same kind three times running.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace IntelEngine::Storylines {

    enum class Kind : std::uint8_t { Meeting = 1, Request = 2, Visit = 3 };

    enum class Request : std::uint8_t {
        None = 0,
        FindPerson,      // find someone and tell the one who asked
        BringItem,       // recover an item for them
        DealWithThreat,  // deal with someone or something at a place
        EscortMe,        // take them somewhere
        DeliverWord,     // speak with someone for them
        Rescue,          // free a captive
    };

    enum class Stage : std::uint8_t {
        Open = 0,      // the objective stands
        ReturnTo = 1,  // done the deed; go back to the one who asked
        Done = 10,
        Declined = 11,
        Ignored = 12,
        Failed = 13,
    };

    // The objective phrasings of each slot in the esp (tools/EspBuilder): objective index = slot * 10 + value.
    enum class Objective : std::uint8_t {
        Meet = 0,
        Find = 1,
        SpeakWith = 2,
        Rescue = 3,
        GoTo = 4,
        ReturnTo = 5,
        Confront = 6,
        Recover = 7,
        WaitFor = 8,
        Escort = 9,
        None = 255,
    };

    // What the scripts report.
    enum class Event : std::uint8_t {
        Achieved = 1,     // the deed is done (found them, got the item, the threat dealt with, the captive free)
        Met = 2,          // the player is with the one who asked (meeting kept, came back, arrived together)
        Declined = 3,     // the player said no in conversation
        TimeRanOut = 4,   // the deadline passed
        SomeoneDied = 5,  // the person it was about died
        Talked = 6,       // the player spoke with the one it was about (a visit, word delivered)
        NoShow = 7,       // the meeting system: the player never came
        FellThrough = 8,  // the meeting system: the one who asked never got there
    };

    struct Storyline {
        std::uint32_t id = 0;
        Kind kind = Kind::Visit;
        Request request = Request::None;
        Stage stage = Stage::Open;
        std::uint8_t slot = 0xFF;         // journal slot, 0xFF = none (visits show no objective)
        std::uint8_t importance = 1;      // 1 small, 2 real, 3 big
        std::uint32_t owner = 0;          // who asked / who wants to meet / who came
        std::uint32_t other = 0;          // the second person: the one to find, the captive, the recipient, the foe
        std::uint32_t place = 0;          // location
        std::uint32_t target = 0;         // what the marker points at
        std::uint32_t item = 0;           // the item to recover
        float created = 0.0f;             // game days
        float stageSince = 0.0f;
        float deadline = 0.0f;            // game days; 0 = none
        bool followedUp = false;          // the director came back to an unhappy ending already
    };

    constexpr std::size_t kSlots = 6;
    constexpr std::size_t kMaxOpen = 6;
    constexpr std::size_t kMaxKept = 32;    // ended storylines are kept a while: memory and follow-ups

    inline bool IsOpen(const Storyline& s) { return s.stage == Stage::Open || s.stage == Stage::ReturnTo; }
    inline bool IsUnhappy(const Storyline& s) {
        return s.stage == Stage::Ignored || s.stage == Stage::Declined || s.stage == Stage::Failed;
    }

    // The objective shown for a storyline in its present stage.
    inline Objective ObjectiveFor(const Storyline& s) {
        if (!IsOpen(s)) return Objective::None;
        if (s.kind == Kind::Visit) return Objective::None;
        if (s.kind == Kind::Meeting) return Objective::Meet;
        if (s.stage == Stage::ReturnTo) return Objective::ReturnTo;
        switch (s.request) {
            case Request::FindPerson: return Objective::Find;
            case Request::BringItem: return Objective::Recover;
            case Request::DealWithThreat: return s.other ? Objective::Confront : Objective::GoTo;
            case Request::EscortMe: return Objective::Escort;
            case Request::DeliverWord: return Objective::SpeakWith;
            case Request::Rescue: return Objective::Rescue;
            default: return Objective::None;
        }
    }

    class Book {
    public:
        std::vector<Storyline> all;
        std::uint32_t nextId = 1;
        std::array<std::uint8_t, 2> lastKeys{0, 0};  // kind * 16 + request of the last two opened
        std::uint8_t opened = 0;  // how many were ever opened (for the kind rotation)

        std::size_t OpenCount() const {
            return static_cast<std::size_t>(std::count_if(all.begin(), all.end(), IsOpen));
        }

        const Storyline* Find(std::uint32_t id) const {
            for (const auto& s : all) {
                if (s.id == id) return &s;
            }
            return nullptr;
        }
        Storyline* Find(std::uint32_t id) {
            for (auto& s : all) {
                if (s.id == id) return &s;
            }
            return nullptr;
        }

        const Storyline* OpenFor(std::uint32_t owner) const {
            for (const auto& s : all) {
                if (IsOpen(s) && (s.owner == owner)) return &s;
            }
            return nullptr;
        }

        static std::uint8_t Key(Kind kind, Request request) {
            return static_cast<std::uint8_t>(static_cast<int>(kind) * 16 + static_cast<int>(request));
        }

        // Why a new storyline may not open (empty = it may). The variety rule is for what the director starts on its
        // own: a meeting, or a request an NPC has just made in conversation (urgent = true), is what was said and must
        // be recorded, or the journal would contradict the dialogue.
        std::string Refusal(Kind kind, Request request, std::uint32_t owner, bool urgent) const {
            if (owner == 0) return "nobody to open it for";
            if (OpenCount() >= kMaxOpen) return "six storylines are open already";
            if (OpenFor(owner)) return "this person has an open storyline already";
            const auto key = Key(kind, request);
            if (!urgent && opened >= 2 && kind == Kind::Request && lastKeys[0] == key && lastKeys[1] == key) {
                return "the same kind three times running";
            }
            return {};
        }

        // Opens a storyline; returns its id, or 0 when Refusal says no. Meetings and requests get a journal slot.
        std::uint32_t Open(Storyline s, float now, bool urgent = false) {
            if (!Refusal(s.kind, s.request, s.owner, urgent).empty()) return 0;
            s.id = nextId++;
            s.stage = Stage::Open;
            s.created = now;
            s.stageSince = now;
            s.followedUp = false;
            s.slot = 0xFF;
            if (s.kind != Kind::Visit) {
                for (std::uint8_t slot = 0; slot < kSlots; ++slot) {
                    const bool used = std::any_of(all.begin(), all.end(), [slot](const Storyline& o) {
                        return IsOpen(o) && o.slot == slot;
                    });
                    if (!used) {
                        s.slot = slot;
                        break;
                    }
                }
                if (s.slot == 0xFF) return 0;  // cannot happen while kMaxOpen <= kSlots
            }
            lastKeys[1] = lastKeys[0];
            lastKeys[0] = Key(s.kind, s.request);
            if (opened < 255) ++opened;
            all.push_back(s);
            Prune();
            return s.id;
        }

        // Applies what the scripts saw; returns true when the storyline changed. Ended storylines ignore events.
        bool Apply(std::uint32_t id, Event event, float now) {
            Storyline* s = Find(id);
            if (!s || !IsOpen(*s)) return false;
            const Stage before = s->stage;
            switch (event) {
                case Event::Achieved:
                    if (s->kind != Kind::Request || s->stage != Stage::Open) break;
                    // Taking someone somewhere ends on arrival; the rest go back to whoever asked.
                    s->stage = s->request == Request::EscortMe ? Stage::Done : Stage::ReturnTo;
                    break;
                case Event::Met:
                    if (s->kind == Kind::Meeting) s->stage = Stage::Done;
                    else if (s->kind == Kind::Request && s->stage == Stage::ReturnTo) s->stage = Stage::Done;
                    else if (s->kind == Kind::Request && s->request == Request::EscortMe) break;
                    else if (s->kind == Kind::Visit) s->stage = Stage::Done;
                    break;
                case Event::Talked:
                    if (s->kind == Kind::Visit) s->stage = Stage::Done;
                    else if (s->kind == Kind::Request && s->request == Request::DeliverWord && s->stage == Stage::Open) {
                        s->stage = Stage::ReturnTo;
                    }
                    break;
                case Event::Declined:
                    s->stage = Stage::Declined;
                    break;
                case Event::TimeRanOut:
                    if (s->deadline > 0.0f && now >= s->deadline) s->stage = Stage::Ignored;
                    break;
                case Event::SomeoneDied:
                    s->stage = Stage::Failed;
                    break;
                case Event::NoShow:
                    if (s->kind == Kind::Meeting) s->stage = Stage::Ignored;
                    break;
                case Event::FellThrough:
                    if (s->kind == Kind::Meeting) s->stage = Stage::Failed;
                    break;
            }
            if (s->stage == before) return false;
            s->stageSince = now;
            return true;
        }

        // Storylines whose deadline passed (the scripts then report TimeRanOut for each).
        std::vector<std::uint32_t> Overdue(float now) const {
            std::vector<std::uint32_t> ids;
            for (const auto& s : all) {
                if (IsOpen(s) && s.deadline > 0.0f && now >= s.deadline) ids.push_back(s.id);
            }
            return ids;
        }

        // Unfinished business between this person and the player: an open storyline, or an unhappy ending the
        // director has not come back to yet.
        bool Unresolved(std::uint32_t owner) const {
            return std::any_of(all.begin(), all.end(), [owner](const Storyline& s) {
                return s.owner == owner && (IsOpen(s) || (IsUnhappy(s) && !s.followedUp));
            });
        }

        void MarkFollowedUp(std::uint32_t owner) {
            for (auto& s : all) {
                if (s.owner == owner && IsUnhappy(s)) s.followedUp = true;
            }
        }

    private:
        // Keep every open storyline and the most recent ended ones.
        void Prune() {
            if (all.size() <= kMaxKept) return;
            std::stable_sort(all.begin(), all.end(), [](const Storyline& a, const Storyline& b) {
                if (IsOpen(a) != IsOpen(b)) return IsOpen(a);
                return a.stageSince > b.stageSince;
            });
            all.resize(kMaxKept);
        }
    };

    // ------------------------------------------------------------------ what people remember

    // A first-person memory for the one who asked, from the record alone (no model call). {player}, {other},
    // {place} and {item} are filled in by the scripts with the real names. Empty = nothing to remember.
    inline std::string OwnerMemory(const Storyline& s) {
        switch (s.kind) {
            case Kind::Meeting:
                // Meetings made by the meeting system are remembered by its own bio module (0199); these are for
                // the rest. A meeting that fell through on the asker's side is not the player's doing: nothing here.
                if (s.stage == Stage::Failed) return {};
                if (s.stage == Stage::Done) return "I met {player} at {place} as we had agreed.";
                if (s.stage == Stage::Ignored) return "I waited at {place} for {player}, who never came.";
                if (s.stage == Stage::Declined) return "I asked {player} to meet me at {place}, and {player} said no.";
                return {};
            case Kind::Request: {
                const char* what = "";
                switch (s.request) {
                    case Request::FindPerson: what = "find {other}"; break;
                    case Request::BringItem: what = "bring me {item}"; break;
                    case Request::DealWithThreat: what = s.other ? "deal with {other}" : "deal with the trouble at {place}"; break;
                    case Request::EscortMe: what = "take me to {place}"; break;
                    case Request::DeliverWord: what = "speak with {other} for me"; break;
                    case Request::Rescue: what = "free {other}"; break;
                    default: what = "help me"; break;
                }
                std::string w = what;
                if (s.stage == Stage::Done) return "I asked {player} to " + w + ", and {player} did it.";
                if (s.stage == Stage::Declined) return "I asked {player} to " + w + ", and {player} refused.";
                if (s.stage == Stage::Ignored) return "I asked {player} to " + w + ", but {player} never did.";
                if (s.stage == Stage::Failed) return "I asked {player} to " + w + ", and it ended badly: {other} is dead.";
                return {};
            }
            case Kind::Visit:
                return {};  // the visit itself is SkyrimNet's conversation; nothing extra to remember
        }
        return {};
    }

    // What the people of the asker's hold heard of how it ended (a SkyrimNet world-knowledge entry, past tense so it
    // stays true): {owner}, {them} (her or him), {player}, {other}, {item}, {place} are filled in. Requests only; a
    // meeting is private.
    inline std::string PublicDeed(const Storyline& s) {
        if (s.kind != Kind::Request) return {};
        const char* what = "";
        switch (s.request) {
            case Request::FindPerson: what = "find {other}"; break;
            case Request::BringItem: what = "bring {them} {item}"; break;
            case Request::DealWithThreat: what = s.other ? "deal with {other}" : "deal with the trouble at {place}"; break;
            case Request::EscortMe: what = "take {them} to {place}"; break;
            case Request::DeliverWord: what = "speak with {other} for {them}"; break;
            case Request::Rescue: what = "free {other}"; break;
            default: what = "help {them}"; break;
        }
        const std::string w = what;
        if (s.stage == Stage::Done) return "{owner} asked {player} to " + w + ", and {player} did it.";
        if (s.stage == Stage::Declined) return "{owner} asked {player} to " + w + ", and {player} refused.";
        if (s.stage == Stage::Ignored) return "{owner} asked {player} to " + w + ", and {player} never did.";
        if (s.stage == Stage::Failed) return "{owner} asked {player} to " + w + ", and it ended badly: {other} is dead.";
        return {};
    }

    // Gold for a request done, by importance and the player's level.
    inline int RewardGold(const Storyline& s, int playerLevel) {
        if (s.kind != Kind::Request || s.stage != Stage::Done) return 0;
        static constexpr int base[] = {0, 40, 120, 300};
        const int importance = std::clamp<int>(s.importance, 1, 3);
        const float scale = 1.0f + std::clamp(playerLevel, 1, 81) / 20.0f;
        return static_cast<int>(std::lround(base[importance] * scale / 5.0f) * 5);
    }

    // ------------------------------------------------------------------ save data (co-save record 'IESL')

    constexpr std::uint32_t kRecordType = 'IESL';
    constexpr std::uint32_t kRecordVersion = 1;

    namespace detail {
        template <class T>
        void Put(std::vector<std::uint8_t>& out, const T& value) {
            const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
            out.insert(out.end(), p, p + sizeof(T));
        }
        template <class T>
        bool Get(std::span<const std::uint8_t> in, std::size_t& at, T& value) {
            if (at > in.size() || in.size() - at < sizeof(T)) return false;
            std::memcpy(&value, in.data() + at, sizeof(T));
            at += sizeof(T);
            return true;
        }
    }

    inline std::vector<std::uint8_t> Serialize(const Book& book) {
        std::vector<std::uint8_t> out;
        detail::Put(out, book.nextId);
        detail::Put(out, book.lastKeys[0]);
        detail::Put(out, book.lastKeys[1]);
        detail::Put(out, book.opened);
        const auto count = static_cast<std::uint32_t>(std::min(book.all.size(), kMaxKept));
        detail::Put(out, count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto& s = book.all[i];
            detail::Put(out, s.id);
            detail::Put(out, static_cast<std::uint8_t>(s.kind));
            detail::Put(out, static_cast<std::uint8_t>(s.request));
            detail::Put(out, static_cast<std::uint8_t>(s.stage));
            detail::Put(out, s.slot);
            detail::Put(out, s.importance);
            detail::Put(out, static_cast<std::uint8_t>(s.followedUp ? 1 : 0));
            detail::Put(out, s.owner);
            detail::Put(out, s.other);
            detail::Put(out, s.place);
            detail::Put(out, s.target);
            detail::Put(out, s.item);
            detail::Put(out, s.created);
            detail::Put(out, s.stageSince);
            detail::Put(out, s.deadline);
        }
        return out;
    }

    // All or nothing, as LivingWorld::Deserialize.
    inline bool Deserialize(std::span<const std::uint8_t> in, Book& book) {
        std::size_t at = 0;
        Book read;
        std::uint8_t k0 = 0, k1 = 0;
        std::uint32_t count = 0;
        if (!detail::Get(in, at, read.nextId) || !detail::Get(in, at, k0) || !detail::Get(in, at, k1) ||
            !detail::Get(in, at, read.opened) || !detail::Get(in, at, count) || count > kMaxKept) {
            return false;
        }
        const auto validKind = [](std::uint8_t k) { return k >= 1 && k <= 3; };
        read.lastKeys = {k0, k1};
        read.all.resize(count);
        for (auto& s : read.all) {
            std::uint8_t kind = 0, request = 0, stage = 0, followed = 0;
            if (!detail::Get(in, at, s.id) || !detail::Get(in, at, kind) || !detail::Get(in, at, request) ||
                !detail::Get(in, at, stage) || !detail::Get(in, at, s.slot) || !detail::Get(in, at, s.importance) ||
                !detail::Get(in, at, followed) || !detail::Get(in, at, s.owner) || !detail::Get(in, at, s.other) ||
                !detail::Get(in, at, s.place) || !detail::Get(in, at, s.target) || !detail::Get(in, at, s.item) ||
                !detail::Get(in, at, s.created) || !detail::Get(in, at, s.stageSince) || !detail::Get(in, at, s.deadline)) {
                return false;
            }
            const bool stageOk = stage <= 1 || (stage >= 10 && stage <= 13);
            if (!validKind(kind) || request > static_cast<std::uint8_t>(Request::Rescue) || !stageOk ||
                (s.slot != 0xFF && s.slot >= kSlots) || !std::isfinite(s.deadline)) {
                return false;
            }
            s.kind = static_cast<Kind>(kind);
            s.request = static_cast<Request>(request);
            s.stage = static_cast<Stage>(stage);
            s.followedUp = followed != 0;
        }
        if (at != in.size()) return false;
        book = std::move(read);
        return true;
    }
}
