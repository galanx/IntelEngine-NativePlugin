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
//  * Branches come from conversation (SkyrimNet actions the dialogue model picks, no extra model call): the one who
//    asked may call it off, give more time, hurry it, raise the reward or want another person, item or place; the
//    one it is about may send an answer back through the player or come along. Each change is recorded, shown in the
//    journal and in the bios, and remembered, so the talk and the record never part ways.

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
        Withdrawn = 14,  // the one who asked called it off (no longer needed): nobody's fault, nothing to follow up
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
        CalledOff = 9,    // the one who asked no longer needs it (said in conversation)
    };

    // What the one who asked may change in conversation while it is open.
    enum class Change : std::uint8_t {
        MoreTime = 1,     // two more days, at most a week from now
        Hurry = 2,        // it has become urgent: half a day from now
        RaiseReward = 3,  // more gold, at most three times what was offered
        NewSubject = 4,   // another person to find or speak with, another item, another place to be taken to
    };

    // Flags of Storyline::changes: what changed since it opened.
    namespace Changed {
        constexpr std::uint8_t Hurried = 1;
        constexpr std::uint8_t MoreTime = 2;
        constexpr std::uint8_t Raised = 4;
        constexpr std::uint8_t NewSubject = 8;
        constexpr std::uint8_t ComesAlong = 16;  // the one it is about goes with the player to the one who asked
    }

    constexpr std::size_t kMaxNote = 160;
    constexpr std::size_t kMaxReply = 200;

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
        std::uint32_t promised = 0;       // gold offered for it, fixed when it opens (raised in talk); 0 = by importance
        std::uint8_t changes = 0;         // Changed:: flags
        std::uint32_t before = 0;         // what was asked before a new subject: the person, the item or the place
        std::string note;                 // the asker's reason for the last change, in their words
        std::string reply;                // the answer the one it was about sent back through the player
    };

    namespace detail {
        // One line of plain text, at most this long, cut at a word.
        inline std::string Clean(const std::string& text, std::size_t most) {
            std::string out;
            for (const char c : text) {
                const char d = (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
                if (d == ' ' && (out.empty() || out.back() == ' ')) continue;
                out += d;
            }
            while (!out.empty() && out.back() == ' ') out.pop_back();
            if (out.size() > most) {
                auto cut = out.rfind(' ', most);
                out = out.substr(0, cut == std::string::npos || cut < most / 2 ? most : cut);
            }
            // what a dialogue model writes for "nothing"
            if (out == "none" || out == "None" || out == "\"\"" || out == "-") out.clear();
            return out;
        }

        // A reason, shown after a colon and ended with a full stop by the text around it: no end mark of its own.
        inline std::string CleanNote(const std::string& text) {
            auto out = Clean(text, kMaxNote);
            while (!out.empty() && (out.back() == '.' || out.back() == '!' || out.back() == ',' || out.back() == ';')) out.pop_back();
            return out;
        }
    }

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
                case Event::CalledOff:
                    if (s->kind == Kind::Request && s->stage == Stage::Open) s->stage = Stage::Withdrawn;
                    break;
            }
            if (s->stage == before) return false;
            s->stageSince = now;
            return true;
        }

        // The one who asked calls it off (no longer needed, settled another way). Only before the deed is done.
        bool CallOff(std::uint32_t id, const std::string& note, float now) {
            Storyline* s = Find(id);
            if (!s || s->kind != Kind::Request || s->stage != Stage::Open) return false;
            s->note = detail::CleanNote(note);
            return Apply(id, Event::CalledOff, now);
        }

        struct Amendment {
            Change change = Change::MoreTime;
            std::uint32_t other = 0;  // NewSubject: the person to find or speak with
            std::uint32_t place = 0;  // NewSubject: where it is now
            std::uint32_t target = 0; // NewSubject: the place to be taken to (the marker)
            std::uint32_t item = 0;   // NewSubject: the item
            int gold = 0;             // RaiseReward: the sum named
            int offered = 0;          // RaiseReward: what is offered now (OfferedGold)
            std::string note;
        };

        // Changes an open request as its asker said in conversation; returns why not (empty = changed).
        std::string Amend(std::uint32_t id, const Amendment& a, float now) {
            Storyline* s = Find(id);
            if (!s || !IsOpen(*s)) return "nothing is open";
            if (s->kind != Kind::Request) return "only a request can change";
            switch (a.change) {
                case Change::MoreTime: {
                    if (s->deadline <= 0.0f) return "it has no time limit";
                    const float later = std::min(std::max(s->deadline, now) + 2.0f, now + 7.0f);
                    if (later <= s->deadline + 0.01f) return "it has as much time as it can have already";
                    s->deadline = later;
                    s->changes = static_cast<std::uint8_t>((s->changes | Changed::MoreTime) & ~Changed::Hurried);
                    break;
                }
                case Change::Hurry: {
                    const float soon = now + 0.5f;
                    if (s->deadline > 0.0f && s->deadline <= soon + 0.01f) return "it is that urgent already";
                    s->deadline = soon;
                    s->changes = static_cast<std::uint8_t>((s->changes | Changed::Hurried) & ~Changed::MoreTime);
                    break;
                }
                case Change::RaiseReward: {
                    const int offered = std::max(a.offered, 1);
                    if (a.gold <= offered) return "that is not more than the " + std::to_string(offered) + " gold offered already";
                    s->promised = static_cast<std::uint32_t>(std::min(a.gold, offered * 3));
                    s->changes |= Changed::Raised;
                    break;
                }
                case Change::NewSubject: {
                    if (s->stage != Stage::Open) return "it is done already";
                    switch (s->request) {
                        case Request::FindPerson:
                        case Request::DeliverWord:
                            if (!a.other || a.other == s->owner) return "nobody else to ask about";
                            if (a.other == s->other) return "that is who it was about already";
                            s->before = s->other;
                            s->other = a.other;
                            s->place = a.place;
                            break;
                        case Request::BringItem:
                            if (!a.item) return "no such item";
                            if (a.item == s->item) return "that is what was asked for already";
                            s->before = s->item;
                            s->item = a.item;
                            break;
                        case Request::EscortMe:
                            if (!a.target) return "no such place";
                            if (a.target == s->target) return "that is where it was to go already";
                            s->before = s->place;
                            s->place = a.place;
                            s->target = a.target;
                            break;
                        default:
                            return "this kind of request cannot change";
                    }
                    if (s->deadline > 0.0f) s->deadline = std::max(s->deadline, now + 1.0f);
                    s->changes |= Changed::NewSubject;
                    break;
                }
                default:
                    return "unknown change";
            }
            const auto note = detail::CleanNote(a.note);
            if (!note.empty()) s->note = note;
            return {};
        }

        // The one it was about answers through the player (finding them or speaking with them is the deed, so an
        // open request moves on to going back); returns why not (empty = recorded).
        std::string Reply(std::uint32_t id, std::uint32_t speaker, const std::string& text, bool comesAlong, float now) {
            Storyline* s = Find(id);
            if (!s || !IsOpen(*s)) return "nothing is open";
            if (s->kind != Kind::Request || (s->request != Request::FindPerson && s->request != Request::DeliverWord)) {
                return "nobody waits for an answer";
            }
            if (!speaker || speaker != s->other) return "it is not about them";
            const auto reply = detail::Clean(text, kMaxReply);
            if (reply.empty() && !comesAlong) return "no answer";
            if (!reply.empty()) s->reply = reply;
            if (comesAlong) s->changes |= Changed::ComesAlong;
            if (s->stage == Stage::Open) {
                s->stage = Stage::ReturnTo;
                s->stageSince = now;
            }
            return {};
        }

        // The open request this person is the subject of (to find or to speak with), 0 = none.
        std::uint32_t AboutId(std::uint32_t person) const {
            for (const auto& s : all) {
                if (IsOpen(s) && s.kind == Kind::Request && person && s.other == person &&
                    (s.request == Request::FindPerson || s.request == Request::DeliverWord)) {
                    return s.id;
                }
            }
            return 0;
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
                if ((s.changes & Changed::NewSubject) && s.before) w += " (at first it was {before})";
                std::string end;
                if (s.stage == Stage::Done) {
                    end = "I asked {player} to " + w + ", and {player} did it.";
                    if (!s.reply.empty()) end += " {player} brought me {other}'s answer: \"" + s.reply + "\"";
                    if (s.changes & Changed::ComesAlong) end += " {other} came along with {player} to see me.";
                    return end;
                }
                if (s.stage == Stage::Declined) return "I asked {player} to " + w + ", and {player} refused.";
                if (s.stage == Stage::Ignored) return "I asked {player} to " + w + ", but {player} never did.";
                if (s.stage == Stage::Failed) return "I asked {player} to " + w + ", and it ended badly: {other} is dead.";
                if (s.stage == Stage::Withdrawn) {
                    return "I asked {player} to " + w + ", but then I called it off" + (s.note.empty() ? "." : ": " + s.note + ".");
                }
                return {};
            }
            case Kind::Visit:
                return {};  // the visit itself is SkyrimNet's conversation; nothing extra to remember
        }
        return {};
    }

    // What the one a request was about remembers: the answer they sent back through the player, or going along.
    // {owner} and {player} are filled in. Empty = nothing.
    inline std::string OtherMemory(const Storyline& s) {
        if (s.kind != Kind::Request || (s.request != Request::FindPerson && s.request != Request::DeliverWord)) return {};
        if (s.reply.empty() && !(s.changes & Changed::ComesAlong)) return {};
        std::string out = s.request == Request::FindPerson ? "{owner} had asked {player} to find me."
                                                           : "{owner} had asked {player} to speak with me.";
        if (!s.reply.empty()) out += " I gave {player} my answer for {owner}: \"" + s.reply + "\"";
        if (s.changes & Changed::ComesAlong) out += " I went along with {player} to see {owner}.";
        return out;
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

    // Gold offered for a request: what was promised (fixed when it opened, raised in talk), else by importance and
    // the player's level.
    inline int OfferedGold(const Storyline& s, int playerLevel) {
        if (s.kind != Kind::Request) return 0;
        if (s.promised > 0) return static_cast<int>(s.promised);
        static constexpr int base[] = {0, 40, 120, 300};
        const int importance = std::clamp<int>(s.importance, 1, 3);
        const float scale = 1.0f + std::clamp(playerLevel, 1, 81) / 20.0f;
        return static_cast<int>(std::lround(base[importance] * scale / 5.0f) * 5);
    }

    // Gold for a request done.
    inline int RewardGold(const Storyline& s, int playerLevel) {
        if (s.kind != Kind::Request || s.stage != Stage::Done) return 0;
        return OfferedGold(s, playerLevel);
    }

    // ------------------------------------------------------------------ save data (co-save record 'IESL')

    constexpr std::uint32_t kRecordType = 'IESL';
    constexpr std::uint32_t kRecordVersion = 2;  // 2: promised, changes, before, note, reply (4.0 branches); 1 still loads

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
        inline void PutText(std::vector<std::uint8_t>& out, const std::string& text, std::size_t most) {
            const auto size = static_cast<std::uint16_t>(std::min(text.size(), most));
            Put(out, size);
            out.insert(out.end(), text.begin(), text.begin() + size);
        }
        inline bool GetText(std::span<const std::uint8_t> in, std::size_t& at, std::string& text, std::size_t most) {
            std::uint16_t size = 0;
            if (!Get(in, at, size) || size > most || in.size() - at < size) return false;
            text.assign(reinterpret_cast<const char*>(in.data() + at), size);
            at += size;
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
            detail::Put(out, s.promised);
            detail::Put(out, s.changes);
            detail::Put(out, s.before);
            detail::PutText(out, s.note, kMaxNote);
            detail::PutText(out, s.reply, kMaxReply);
        }
        return out;
    }

    // All or nothing, as LivingWorld::Deserialize. Version 1 records have no branch fields.
    inline bool Deserialize(std::span<const std::uint8_t> in, Book& book, std::uint32_t version = kRecordVersion) {
        if (version < 1 || version > kRecordVersion) return false;
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
            if (version >= 2 && (!detail::Get(in, at, s.promised) || !detail::Get(in, at, s.changes) || !detail::Get(in, at, s.before) ||
                                 !detail::GetText(in, at, s.note, kMaxNote) || !detail::GetText(in, at, s.reply, kMaxReply))) {
                return false;
            }
            const bool stageOk = stage <= 1 || (stage >= 10 && stage <= 14);
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
