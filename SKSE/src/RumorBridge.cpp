#include "RumorBridge.h"
#include "FrameCost.h"
#include "LivingWorldBridge.h"
#include "Safe.h"
#include "SkyrimNetAPI.h"

#include <cctype>
#include <chrono>
#include <thread>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace IntelEngine {

    namespace R = Rumors;

    namespace {
        // The label of an entry this bridge wrote for a hold: "IntelEngine rumor #<rumor>@<hold>".
        constexpr std::string_view kLabel = "IntelEngine rumor #";
        // Who hears everything (the game's own job factions, Skyrim.esm).
        constexpr RE::FormID kInnkeepers = 0x0005091B, kServers = 0x000DEE93, kBards = 0x00053514, kMerchants = 0x00051596;

        float NowDays() {
            auto* calendar = RE::Calendar::GetSingleton();
            return calendar ? calendar->GetCurrentGameTime() : 0.0f;
        }

        std::uint8_t HoldOf(RE::BGSLocation* location) {
            int depth = 0;
            for (auto* loc = location; loc && depth < 12; loc = loc->parentLoc, ++depth) {
                if (const auto hold = R::HoldIndex(loc->GetFormID()); hold != R::NoHold) return hold;
            }
            return R::NoHold;
        }

        std::string HoldName(std::uint8_t hold) {
            auto* loc = hold < R::kHolds ? RE::TESForm::LookupByID<RE::BGSLocation>(R::kHoldLocation[hold]) : nullptr;
            const char* name = loc ? loc->GetFullName() : nullptr;
            return name ? name : "";
        }

        std::string NameOf(RE::FormID ref) {
            auto* actor = ref ? RE::TESForm::LookupByID<RE::Actor>(ref) : nullptr;
            return actor ? Safe::Name(actor->GetDisplayFullName()) : std::string();
        }

        std::string Escape(const std::string& text) {
            std::string out;
            out.reserve(text.size());
            for (const char c : text) {
                if (c == '"' || c == '\\') out += '\\';
                out += c;
            }
            return out;
        }

        bool In(RE::Actor* actor, RE::FormID factionId) {
            auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(factionId);
            return actor && faction && actor->IsInFaction(faction);
        }

        // 3 the innkeeper, 2 who serves there, 1 a bard or a merchant, 0 anyone else.
        int Hears(RE::Actor* actor) { return In(actor, kInnkeepers) ? 3 : In(actor, kServers) ? 2 : (In(actor, kBards) || In(actor, kMerchants)) ? 1 : 0; }

        const char* KindWord(R::Kind kind) {
            return kind == R::Kind::Deed ? "a deed" : kind == R::Kind::Trouble ? "trouble" : kind == R::Kind::Life ? "a life" : "news";
        }

        // What SkyrimNet is told for one hold, put together on the calling thread (names are the game's to give).
        struct Entry {
            std::uint32_t rumor = 0;
            std::uint8_t hold = R::NoHold;
            std::string content, condition, label;
            float importance = 0.45f;
        };

        // What is said of it in a hold: away from home it is hearsay twice over, and says where it comes from.
        std::string TextIn(const R::Rumor& r, std::uint8_t hold) {
            return hold == r.home ? r.text : r.text + " (So say travellers come from " + HoldName(r.home) + "; nobody here saw it.)";
        }

        Entry EntryFor(const R::Rumor& r, std::uint8_t hold) {
            Entry e;
            e.rumor = r.id;
            e.hold = hold;
            const std::string here = HoldName(hold);
            e.content = TextIn(r, hold);
            e.condition = "get_intelengine_hold(actorUUID) == \"" + Escape(here) + "\"";
            for (const auto who : {r.about, r.lead}) {
                if (const auto name = NameOf(who); !name.empty()) e.condition += " and actorName != \"" + Escape(name) + "\"";
            }
            e.label = std::string(kLabel) + std::to_string(r.id) + "@" + std::to_string(static_cast<int>(hold));
            e.importance = 0.3f + 0.3f * r.weight;
            if (here.empty()) e.rumor = 0;
            return e;
        }
    }

    RumorBridge* RumorBridge::GetSingleton() {
        static RumorBridge instance;
        return &instance;
    }

    std::uint32_t RumorBridge::Note(R::Kind kind, const std::string& text, RE::FormID location, RE::FormID about, RE::FormID lead, R::Owner owner,
                                    std::uint32_t ownerId, float weight, bool ownEntry) {
        const auto hold = HoldOf(RE::TESForm::LookupByID<RE::BGSLocation>(location));
        if (hold == R::NoHold || text.empty()) return 0;
        R::Rumor r;
        r.kind = kind;
        r.text = text;
        r.home = hold;
        r.about = about;
        r.lead = lead;
        r.owner = owner;
        r.ownerId = ownerId;
        r.weight = weight;
        std::vector<R::Rumor> dropped;
        std::vector<R::Event> events;
        std::uint32_t id = 0;
        {
            std::scoped_lock lock(m_mutex);
            id = m_ledger.Add(std::move(r), NowDays(), &dropped);
            if (id && ownEntry) events.push_back({R::Event::Reached, *m_ledger.Find(id), hold});
        }
        if (!id) return 0;
        logger::info("Rumors: #{} goes round {} ({}, weight {:.2f}): {}", id, HoldName(hold), KindWord(kind), weight, text);
        if (!events.empty() || !dropped.empty()) Write(std::move(events), std::move(dropped));
        return id;
    }

    std::uint32_t RumorBridge::NoteNews(const std::string& text, const std::string& where, float weight, std::uint32_t ownerId) {
        static constexpr std::pair<const char*, std::uint8_t> kNames[] = {
            {"whiterun", R::Whiterun},   {"falkreath", R::Falkreath},   {"markarth", R::Reach},        {"reach", R::Reach},
            {"solitude", R::Haafingar},  {"haafingar", R::Haafingar},   {"morthal", R::Hjaalmarch},    {"hjaalmarch", R::Hjaalmarch},
            {"dawnstar", R::Pale},       {"pale", R::Pale},             {"winterhold", R::Winterhold}, {"windhelm", R::Eastmarch},
            {"eastmarch", R::Eastmarch}, {"riften", R::Rift},           {"rift", R::Rift}};
        std::string lower = where;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::uint8_t hold = m_playerHold.load();
        for (const auto& [name, index] : kNames) {
            if (lower.find(name) != std::string::npos) {
                hold = index;
                break;
            }
        }
        if (hold >= R::kHolds) return 0;
        return Note(R::Kind::News, text, R::kHoldLocation[hold], 0, 0, R::Owner::Politics, ownerId, weight, false);
    }

    void RumorBridge::Withdraw(R::Owner owner, std::uint32_t ownerId, R::Kind only) {
        std::vector<R::Rumor> gone;
        {
            std::scoped_lock lock(m_mutex);
            gone = m_ledger.Withdraw(owner, ownerId, only);
        }
        if (gone.empty()) return;
        for (const auto& r : gone) logger::info("Rumors: #{} is taken back ({} of {} #{})", r.id, KindWord(r.kind), static_cast<int>(owner), ownerId);
        Write({}, std::move(gone));
    }

    std::vector<std::uint32_t> RumorBridge::Owners(R::Owner owner, R::Kind kind) const {
        std::vector<std::uint32_t> ids;
        std::scoped_lock lock(m_mutex);
        for (const auto& r : m_ledger.all) {
            if (r.owner == owner && r.kind == kind) ids.push_back(r.ownerId);
        }
        return ids;
    }

    void RumorBridge::Tick() {
        // Inside a frame of the game, every two seconds: measured, and said when it is ever slow.
        static FrameStats cost;
        const FrameClock clock("Rumors: a look at the talk of the day", cost, FrameClock::Now());
        const float now = NowDays();
        m_now.store(now);
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            if (const auto hold = HoldOf(player->GetCurrentLocation()); hold != R::NoHold) m_playerHold.store(hold);
        }
        std::vector<R::Event> reached;
        std::vector<R::Rumor> gone;
        bool reconcile = false;
        {
            std::scoped_lock lock(m_mutex);
            if (!m_reconciled && std::chrono::steady_clock::now() - m_loadedAt > std::chrono::seconds(25)) {
                m_reconciled = true;
                reconcile = true;
            }
            // Once a game hour is often enough for talk to move; a clock set back (a load) starts it afresh.
            if (m_lastTick < 0.0f || now < m_lastTick || now - m_lastTick >= 1.0f / 24.0f) {
                m_lastTick = now;
                for (auto& e : m_ledger.Tick(now)) {
                    if (e.type == R::Event::Reached) reached.push_back(std::move(e));
                    else gone.push_back(std::move(e.rumor));
                }
            }
        }
        if (reconcile) Reconcile();
        for (const auto& e : reached) logger::info("Rumors: #{} has reached {} (from {})", e.rumor.id, HoldName(e.hold), HoldName(e.rumor.home));
        for (const auto& r : gone) logger::info("Rumors: #{} is no longer talked of", r.id);
        if (!reached.empty() || !gone.empty()) Write(std::move(reached), std::move(gone));
    }

    void RumorBridge::Write(std::vector<R::Event> events, std::vector<R::Rumor> gone) {
        if (!SkyrimNetAPI::AddWorldKnowledge || !SkyrimNetAPI::RemoveWorldKnowledge) return;
        std::vector<Entry> entries;
        for (const auto& e : events) {
            if (auto entry = EntryFor(e.rumor, e.hold); entry.rumor) entries.push_back(std::move(entry));
        }
        std::vector<int> removals;
        for (const auto& r : gone) {
            for (const auto k : r.entry) if (k > 0) removals.push_back(k);
        }
        if (entries.empty() && removals.empty()) return;
        std::thread([this, entries = std::move(entries), removals = std::move(removals)]() {
            for (const auto k : removals) SkyrimNetAPI::RemoveWorldKnowledge(k);
            for (const auto& e : entries) {
                const int k = SkyrimNetAPI::AddWorldKnowledge(e.content.c_str(), e.condition.c_str(), false, e.importance, e.label.c_str());
                if (k <= 0) continue;
                bool kept = false;
                {
                    std::scoped_lock lock(m_mutex);
                    if (auto* r = m_ledger.Find(e.rumor); r && r->entry[e.hold] == 0) {
                        r->entry[e.hold] = k;
                        kept = true;
                    }
                }
                // Taken back while it was being written: it goes at once.
                if (!kept) SkyrimNetAPI::RemoveWorldKnowledge(k);
            }
        }).detach();
    }

    void RumorBridge::Reconcile() {
        // What SkyrimNet holds of rumors this save does not have (written after the save was made, in a timeline the
        // player left by loading): withdrawn, as the quests, stories and lives do for theirs.
        if (!SkyrimNetAPI::GetWorldKnowledge || !SkyrimNetAPI::RemoveWorldKnowledge) return;
        std::unordered_set<int> ours;
        {
            std::scoped_lock lock(m_mutex);
            for (const auto& r : m_ledger.all) {
                for (const auto k : r.entry) if (k > 0) ours.insert(k);
            }
        }
        std::thread([ours = std::move(ours)]() {
            const auto entries = nlohmann::json::parse(SkyrimNetAPI::GetWorldKnowledge(100000), nullptr, false);
            if (!entries.is_array()) return;
            int withdrawn = 0;
            for (const auto& entry : entries) {
                const int id = Safe::Int(entry, "id");
                if (id <= 0 || !Safe::Text(entry, "display_name").starts_with(kLabel) || ours.contains(id)) continue;
                if (SkyrimNetAPI::RemoveWorldKnowledge(id)) ++withdrawn;
            }
            logger::info("Rumors: {} rumor(s) of other timelines withdrawn, {} kept", withdrawn, ours.size());
        }).detach();
    }

    std::string RumorBridge::Lines(RE::Actor* actor) {
        if (!actor || actor->IsPlayerRef()) return {};
        const auto hold = HoldOf(actor->GetCurrentLocation());
        if (hold == R::NoHold) return {};
        const int hears = Hears(actor);
        const float now = m_now.load();
        std::vector<std::string> lines;
        {
            std::scoped_lock lock(m_mutex);
            for (const R::Rumor* r : m_ledger.ForHold(hold, actor->GetFormID(), now, hears ? 4u : 1u)) {
                // Those who hear everything know it all; anyone else the talk of the day: trouble, or something with
                // weight that is not yet old.
                if (!hears && r->kind != R::Kind::Trouble && (r->weight < 0.5f || now - r->born > 4.0f)) continue;
                lines.push_back(TextIn(*r, hold));
            }
        }
        if (lines.empty()) return {};
        const std::string name = Safe::Name(actor->GetDisplayFullName());
        std::string out;
        if (hears) {
            out = "## What is going round\nPeople bring their talk to " + name + ". What " + name + " has heard lately:\n";
            for (const auto& line : lines) out += "- " + line + "\n";
            out += "Asked for news or rumors, " + name + " passes on what fits, one thing at a time and in their own words; unasked, only when the "
                   "talk comes near it. It is hearsay: " + name + " saw none of it.";
        } else {
            out = "## What is going round\nLike everyone hereabouts, " + name + " has heard: " + lines.front() +
                  " It is hearsay, worth a word only if the talk comes near it.";
        }
        return out;
    }

    RE::Actor* RumorBridge::Publican() const {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* cell = player ? player->GetParentCell() : nullptr;
        auto* lists = RE::ProcessLists::GetSingleton();
        if (!cell || !cell->IsInteriorCell() || !lists) return nullptr;
        RE::Actor* best = nullptr;
        int bestRank = 0;
        float bestDistance = 0.0f;
        for (const auto& handle : lists->highActorHandles) {
            auto actor = handle.get();
            if (!actor || actor.get() == player || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded() || actor->GetParentCell() != cell) continue;
            if (actor->IsInCombat() || actor->IsPlayerTeammate()) continue;
            if (auto* state = actor->AsActorState(); state && state->GetSitSleepState() == RE::SIT_SLEEP_STATE::kIsSleeping) continue;
            const int rank = Hears(actor.get());
            if (!rank) continue;
            const float distance = actor->GetPosition().GetDistance(player->GetPosition());
            if (distance > 3000.0f) continue;
            if (rank > bestRank || (rank == bestRank && distance < bestDistance)) {
                best = actor.get();
                bestRank = rank;
                bestDistance = distance;
            }
        }
        return best;
    }

    std::string RumorBridge::BringUp(RE::Actor* teller) {
        if (!teller || teller->IsDead()) return {};
        const auto hold = HoldOf(teller->GetCurrentLocation());
        if (hold == R::NoHold) return {};
        const float now = NowDays();
        std::string text;
        std::uint32_t id = 0;
        {
            std::scoped_lock lock(m_mutex);
            if (const R::Rumor* r = m_ledger.ToBringUp(hold, teller->GetFormID(), now)) {
                text = TextIn(*r, hold);
                id = r->id;
                m_ledger.Told(id, now);
            }
        }
        if (id) {
            logger::info("Rumors: {} brings up #{} to the player: {}", Safe::Name(teller->GetDisplayFullName()), id, text);
            // Who told whom is the teller's own memory (SkyrimNet): so they know later that they were the one who
            // said it, and can ask what came of it.
            auto* player = RE::PlayerCharacter::GetSingleton();
            const std::string listener = player ? Safe::Name(player->GetDisplayFullName()) : std::string();
            auto* at = teller->GetCurrentLocation();
            const std::string place = at && at->GetFullName() ? at->GetFullName() : "";
            const RE::FormID who = teller->GetFormID();
            if (!listener.empty()) {
                std::thread([who, listener, place, text]() {
                    if (!SkyrimNetAPI::AddMemory) return;
                    const std::string memory = "I passed on to " + listener + " what was going round: " + text;
                    SkyrimNetAPI::AddMemory(who, memory.c_str(), 0.4f, "EXPERIENCE", "", place.c_str(), "[\"intelengine_rumor\"]", "[]");
                }).detach();
            }
        }
        return text;
    }

    // ------------------------------------------------------------------ the co-save

    void RumorBridge::Save(SKSE::SerializationInterface* intfc) {
        std::vector<std::uint8_t> bytes;
        {
            std::scoped_lock lock(m_mutex);
            bytes = R::Serialize(m_ledger);
        }
        if (!intfc->OpenRecord(kRumorRecordType, R::kRecordVersion)) {
            logger::error("Rumors: could not open the co-save record");
            return;
        }
        intfc->WriteRecordData(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }

    void RumorBridge::Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length) {
        if (version < 1 || version > R::kRecordVersion || length > (1u << 20)) {
            logger::warn("Rumors: skipping a co-save record of version {} ({} bytes)", version, length);
            return;
        }
        std::vector<std::uint8_t> bytes(length);
        if (length > 0 && intfc->ReadRecordData(bytes.data(), length) != length) {
            logger::warn("Rumors: the co-save record is short; starting with nothing going round");
            return;
        }
        R::Ledger ledger;
        if (!R::Deserialize(bytes, ledger, version)) {
            logger::warn("Rumors: the co-save record is damaged; starting with nothing going round");
            return;
        }
        // The people a rumor is about may have moved in the load order.
        for (auto& r : ledger.all) {
            for (auto* who : {&r.about, &r.lead}) {
                RE::FormID resolved = 0;
                if (*who && !intfc->ResolveFormID(*who, resolved)) resolved = 0;
                *who = resolved;
            }
        }
        std::scoped_lock lock(m_mutex);
        m_ledger = std::move(ledger);
        logger::info("Rumors: {} going round in this save", m_ledger.all.size());
    }

    void RumorBridge::Revert() {
        std::scoped_lock lock(m_mutex);
        m_ledger = R::Ledger{};
        m_lastTick = -1.0f;
        m_loadedAt = std::chrono::steady_clock::now();
        m_reconciled = false;
    }

    // ------------------------------------------------------------------ Papyrus

    namespace {
        RE::Actor* RumorPublican(RE::StaticFunctionTag*) {
            // Nobody brings up the talk of the day in the middle of a fight, a scene or a conversation.
            return LivingWorldBridge::IsBadMoment() ? nullptr : RumorBridge::GetSingleton()->Publican();
        }
        RE::BSFixedString RumorBringUp(RE::StaticFunctionTag*, RE::Actor* teller) {
            return RE::BSFixedString(RumorBridge::GetSingleton()->BringUp(teller).c_str());
        }
        // Test hook: something is going round the hold the player is in (trouble with someone to go to, or a deed).
        std::int32_t RumorDebugNote(RE::StaticFunctionTag*, RE::BSFixedString text, RE::Actor* lead, float weight) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* here = player ? player->GetCurrentLocation() : nullptr;
            if (!here || !text.c_str()) return 0;
            return static_cast<std::int32_t>(RumorBridge::GetSingleton()->Note(lead ? R::Kind::Trouble : R::Kind::Deed, text.c_str(), here->GetFormID(), 0,
                                                                              lead ? lead->GetFormID() : 0u, R::Owner::None, 0, weight, true));
        }
    }

    void RumorBridge::Register(RE::BSScript::IVirtualMachine* a_vm, int& count) {
        constexpr const char* script = "IntelEngine";
        a_vm->RegisterFunction("RumorPublican", script, RumorPublican); ++count;
        a_vm->RegisterFunction("RumorBringUp", script, RumorBringUp); ++count;
        a_vm->RegisterFunction("RumorDebugNote", script, RumorDebugNote); ++count;
    }
}
