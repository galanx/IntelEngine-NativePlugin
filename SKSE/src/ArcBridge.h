#pragma once

#include "Arcs.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace IntelEngine {

    /**
     * Story arcs in the game (docs/STORY_ARCS.md): casts an arc from the real people of a town, reads the facts of the
     * game once a look, says where its props go and who the journal points at, and keeps the arcs in the co-save
     * (record 'IEAR'). The decisions are Arcs.h's; IntelEngine_Arcs.psc offers, shows and carries out.
     *
     * Every function that touches the game runs on the main thread (Papyrus natives, the SKSE task queue). LinesFor
     * is read by SkyrimNet's threads: it reads only the record, under the lock.
     */
    class ArcBridge {
    public:
        static ArcBridge* GetSingleton() {
            static ArcBridge instance;
            return &instance;
        }

        /** At data load: the townsfolk with a home in a town, and who a quest of the journal names (never cast as a
         *  culprit or a victim while that quest is not done). */
        void Catalogue();

        /** Plans an arc of this pattern (0: whichever fits) with this asker (main thread). 0 = none fits. With
         *  `hereOnly` (the director), only for someone of the town the player is in now. */
        std::uint32_t PlanFor(RE::Actor* asker, int pattern, bool hereOnly = false);
        /** Whether this actor asked the player for something in an arc that ended (or was turned down) this recently. */
        bool AskedWithin(RE::FormID actor, float days) const;
        bool Accept(std::uint32_t id);
        bool Decline(std::uint32_t id);
        /** A planned arc that never reached the player (the asker could not come): dropped, nobody remembers it. */
        bool Drop(std::uint32_t id);
        std::vector<std::uint32_t> Lapse();
        std::vector<std::uint32_t> OpenIds() const;

        /** Reads the facts and moves the arc on. Bits 0-7 the nodes done now, 8-15 the ending, 16-19 the effect, 20-23
         *  its role; 24 a prop is to be put in place now (PlantAt), 25 the choice may be offered, 26 the hand-over may
         *  be offered; 27-30 a node left by a shortcut, plus one. 0 = nothing. */
        std::int32_t Advance(std::uint32_t id);
        /** The answer to the choice popup, by the index of the node's options (-1: not now). */
        bool Answer(std::uint32_t id, int option);
        /** The prop of the current hand-over goes from the player to who the node gives it to. */
        bool HandOver(std::uint32_t id);
        /** A murder's victim, unseen, taken home to be killed there by the script (Kill credits the killer): the victim,
         *  or None when they are in sight, already dead, or this is no murder. */
        RE::Actor* StageVictim(std::uint32_t id);
        /** Everyone of the arc who speaks within this many seconds counts as speaking with the player: a scene among
         *  them in front of the player (the feud's quarrel when both are in the room). */
        void Scene(std::uint32_t id, float seconds);
        /** How the brawl of a Brawl node ended: 1 the player won, -1 lost (or it turned into a real fight). */
        bool Brawl(std::uint32_t id, int result);
        /** Gold that reached the player from this actor (the inventory event): counted toward an open arc's reward. */
        void GoldToPlayer(RE::FormID from, std::int32_t count);
        /** At data load: the inventory event that sees gold reach the player, and the quest stages that say the
         *  player is busy with a quest. */
        void Listen();
        /** A quest of the journal moved on: no town story for a while. */
        void QuestMovedOn(RE::TESQuest* quest);
        /** The player laid hands on someone (activated them): when a story holds them bound, they are cut loose. */
        void Activated(RE::Actor* actor);
        /** A tale's "give": the stamped prop goes from wherever it was put to the player. False when it was never put
         *  anywhere yet (the script then makes it in the player's hands). */
        bool FetchProp(std::uint32_t id, int prop);
        static RE::TESBoundObject* BossBase(std::uint8_t kind, std::uint32_t seed);

        static RE::TESBoundObject* GangBase(std::uint8_t kind, std::uint32_t seed);
        /** The script placed a place's master (Papyrus' PlaceAtMe resolves the game's leveled lists). */
        void BossPlaced(std::uint32_t id, int site, RE::Actor* boss);
        /** The script moved the one held at a place on (Arcs::CaptiveState: away, bound at the heart, gone home). */
        void Captive(std::uint32_t id, int state);
        /** The script placed the company a look asked for. */
        void GangPlaced(std::uint32_t id);
        /** The script placed one of an ambush's men (k = 0 leads). */
        void ThugPlaced(std::uint32_t id, int k, RE::Actor* thug);
        /** The script did what a look asked of the ambush (1 placed them, 2 they struck, 3 called off unseen). */
        void AmbushDone(std::uint32_t id, int what);
        /** Whether a place out in the world is free for a story: no quest of the game still needs it, none of the
         *  player's objectives points into it, no other story or quest of IntelEngine's is set there. */
        bool SiteFree(RE::BGSLocation* location, const std::unordered_set<RE::FormID>& objectiveLocations, std::string& why) const;
        /** The script let the step's person go (silence, the player left, the step moved on). */
        void LingerOver(std::uint32_t id);
        /** A tale's steps and branches, one log line each (when planned and after a load). */
        static void LogOutline(const Arcs::Arc& a);
        /** The script put prop `prop` at this reference (a container, or the floor beside it). */
        void Planted(std::uint32_t id, int prop, RE::TESObjectREFR* at);

        int Field(std::uint32_t id, const std::string& name) const;
        RE::TESForm* Form(std::uint32_t id, const std::string& name) const;
        std::string Text(std::uint32_t id, const std::string& name) const;

        /** Whether this actor has a part in an arc that is planned or open. */
        bool Involves(RE::FormID actor) const;
        /** The id of the planned or open arc this actor has a part in, 0 = none. */
        std::uint32_t ArcOf(RE::FormID actor) const;
        /** What this NPC knows of the arcs they are in, first person lines ("- ..."), for their bio. */
        std::string LinesFor(RE::Actor* actor) const;
        /** Every journal quest of an arc gets its title (on accepting, and after every load); a tale's also its step
         *  goals as objective texts and its story so far and closing line on the two holders of its slot. */
        void ApplyTitles() const;
        /** The two hidden references whose names a tale's journal log shows (story so far, the ending): the script
         *  makes them per slot and tells them again after every load. */
        void SetHolders(std::uint8_t slot, RE::TESObjectREFR* tale, RE::TESObjectREFR* close);

        // ---- tales (TaleAuthor.cpp): stories the dungeon master writes for a town's people (docs/STORY_ARCS.md 0)

        /** Asks the dungeon master for a tale with this asker (main thread). False when none can be asked for now (one
         *  at a time, no model, nobody to cast); the answer plans the arc and sends IntelEngine_ArcOffer, or falls back
         *  to a pattern. */
        /** Why a tale begins now when the director chose its asker: their own situation as the candidate analyser saw it
         *  (memories, unfinished business, their life), and that they come to the player wherever the player is. */
        struct TaleSeed {
            std::string why;
            std::string kind;
            bool fromAfar = false;
            bool otherHold = false;   // from another hold: counted, so that it stays rare (FarComerLately)
        };
        /** Someone came to the player from another hold with a story within this many game days. Any thread. */
        bool FarComerLately(float days) const;
        /** The offer of someone who came from another hold could not go out now: tried again in an hour. */
        void OfferLater(std::uint32_t id);
        /** The asker set out at a walk and the player was gone before they got there: the story stands, and the offer
         *  goes out when the two next meet (Lapse). */
        void OfferDeferred(std::uint32_t id, const std::string& company = {});
        /** Someone of this story (not its asker) who stands beside the player right now, or null: nobody brings their
            trouble to the player in front of the people it is about. */
        RE::Actor* Company(std::uint32_t id) const;
        /** Where the journal points for a step, as the game stands right now: the one answer for every kind of step,
         *  asked on every watch. Never something switched off or gone from the game; nullptr only where nothing is
         *  to be pointed at. The caller holds the bridge's lock. */
        RE::TESForm* MarkerOf(const Arcs::Arc& a, const Arcs::Node& n) const;
        /** A door of the home of one of the townsfolk (inside it; the game leads to it from outside), or nullptr. */
        RE::TESObjectREFR* HomeDoorOf(RE::FormID actorRef) const;
        /** Whether someone coming to the player has truly been looking for them (and so hurries): close to the
         *  player, or come all the way from another hold. Not someone who merely failed to reach them once. Main
         *  thread. */
        bool Hurries(std::uint32_t id, RE::Actor* actor) const;
        /** 2 has had to do with the player before, 1 their town has heard what the player did there, 0 a stranger.
         *  Main thread. */
        int KnowsPlayer(RE::Actor* actor, RE::FormID town) const;
        bool RequestTale(RE::Actor* asker, const std::string& written = {}, const TaleSeed* seed = nullptr);
        /** Plans a compiled tale (main thread). 0 = refused. */
        std::uint32_t PlanTale(Arcs::Arc a);
        /** The judge's reading of a talk (any thread): which prepared outcome it was, -1 none yet. */
        void Judged(std::uint32_t id, std::uint8_t node, int turn);
        /** Whether a tale is being written now. */
        bool Writing() const { return m_taleBusy.load(); }

        void Save(SKSE::SerializationInterface* intfc);
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

    private:
        ArcBridge() = default;
        static float NowDays();

        struct Folk {
            RE::FormID ref = 0;
            RE::FormID base = 0;
            RE::FormID home = 0;   // the interior cell of their bed
            RE::FormID town = 0;   // the town (location) that home is in
            bool placedHome = false;   // the game's data puts them in that home (MoveToEditorLocation takes them there)
        };
        // What one look learned that the record does not keep (the popups' answers, where a prop goes now).
        struct Pending {
            int answer = -1;
            std::uint8_t handed = 0;
            int brawl = 0;
            std::uint16_t log = 0;
            int effect2 = 0;
            int effect2Role = 0;
            int bondWith = 0;
            int effectProp = 0;
            RE::FormID plantAt = 0;
            int plantProp = -1;
            bool plantFloor = false;
            float chooseHold = -1.0f;   // real seconds: no choice popup before this
            float giveHold = -1.0f;
            float sceneUntil = -1.0f;    // real seconds: a scene among the cast in front of the player
            RE::FormID checkedCell = 0;  // the interior last looked at for the culprit's home, and what it was
            bool homeHere = false;
            std::uint8_t homeOf = 0;     // a tale: whose home that interior is (role), 0 = nobody's of the cast
            // A tale's talk: the judge reads the conversation once it goes quiet.
            std::uint8_t node = 0xFF;    // the step these belong to (a new step starts them afresh)
            int turn = -1;               // the judge's answer, -1 = none yet
            bool talked = false;         // the player and the step's person talked since the last reading
            bool judging = false;        // a reading is on its way
            int nones = 0;               // readings that found no outcome yet
            float judgedAt = -1.0f;      // real seconds of the last reading
            float askedAt = -1.0f;       // real seconds: the player's first line since that reading (< 0: none yet)
            float visitAt = -1.0f;       // game days: the step's person was last sent to the player
            float stalkHold = -1.0f;     // real seconds: a stalker is not sent after the player again before this
            bool tooFar = false;         // the step's visitor or shadow waits: the player is out of the tale's hold (logged once)
            float offerDue = -1.0f;      // game days: someone on the road from another hold reaches the player then (< 0: not set)
            bool offerSent = false;      // ... and their offer went out
            float deferredAt = -1.0f;    // real seconds: the asker gave up reaching the player (not offered again at once)
            bool lingering = false;      // the step's person stays with the player while they talk (the script's)
            float lingerHold = -1.0f;    // real seconds: not asked to linger again before this (refused or let go)
            std::array<std::int8_t, Arcs::kSites> bossState{};   // last seen: 0 unknown, 1 alive and loaded, 2 dead, 3 gone
            std::array<bool, Arcs::kSites> bossSeenAlive{};      // the boss was loaded and alive once (its death then counts)
            std::uint8_t gangSite = 0;                           // a place (index + 1) whose company the script is to place
            RE::FormID siteMarker = 0;                           // where the journal last pointed (any kind of step)
            bool siteMarkerKnown = false;
            std::uint8_t markerNode = 0xFF;                      // the step that was for
            // A tale's ambush: what the script is asked to do (1 place, 2 strike, 3 call off), and when it may be asked.
            std::uint8_t ambushDo = 0;
            float ambushHold = -1.0f;    // real seconds: not placed again before this
            float ambushAgain = -1.0f;   // game days: called off; they find the player again after this
            float wildsSince = -1.0f;    // real seconds: the player has been out in the wilds since
            bool struck = false;         // they stepped out and fight
            float strikeHold = -1.0f;    // real seconds: not told to strike again before this
            int ambushTries = 0;         // placings that found no ground ahead out of sight (then they come from behind)
            std::string ambushWait;      // why the ambush waits now (logged when it changes)
            float placedAt = -1.0f;      // real seconds: its men were placed (or first seen after a load)
            bool chasing = false;        // they are coming after the player (not waiting on the road ahead)
            float callOffHold = -1.0f;   // real seconds: not asked to call them off again before this
            float comeHold = -1.0f;      // real seconds: not sent after the player again before this
            float parleyAt = -1.0f;      // real seconds: the leader stepped out and has their word (< 0: not now)
            float parleyHold = -1.0f;    // real seconds: a menu was open (an answer being typed); nothing decided before this
            bool parleyFight = false;    // what was offered cannot be paid: they fall on the player
            bool shortPurse = false;     // ... and the leader says so
            std::uint8_t standWhy = 0;   // why the men stand down: 1 the player is beaten, 2 talked out of it
        };
        void TaleTalk(std::uint32_t id, const Arcs::Arc& a, Pending& pending, const Arcs::Facts& f);   // lock held
        /** A tale's ambush step, each look (lock held): when its men are placed, step out, or are called off. */
        void AmbushLook(std::uint32_t id, Arcs::Arc& a, Pending& pending, const Arcs::Node& n, float now, int placed);
        /** The leader's word and what the player says to it (lock held): true when it is over and they fall on the
         *  player, with why. A talk in between is read like a talk step's (SendJudge). */
        bool Parley(std::uint32_t id, const Arcs::Arc& a, Pending& pending, const Arcs::Node& n, float nearest, std::string& why);
        void SendTale(std::shared_ptr<void> request, int attempt, std::string fix, std::string previous);   // any thread
        void TaleWritten(std::shared_ptr<void> request, int attempt, const std::string& text);              // main thread
        void SendOffer(std::uint32_t id, RE::Actor* asker);
        void SendJudge(std::uint32_t id, std::uint8_t node, RE::FormID who, std::string goal, std::array<std::string, 3> turns,
                       bool force);
        /** The game's own small quests (favours with a name) of each person, by base: for the tale author only. */
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_favours;
        std::atomic<bool> m_taleBusy{false};
        std::atomic<float> m_questBusyUntil{-1.0f};   // real seconds: a quest moved on lately
        std::array<std::array<RE::FormID, 2>, Arcs::kSlots> m_holders{};

        std::string Fill(std::string text, const Arcs::Arc& a) const;
        const Folk* FolkOf(RE::FormID ref) const;
        bool Usable(RE::Actor* actor, int need, std::string& why) const;
        bool Cast(Arcs::Arc& a, RE::Actor* asker, std::string& why) const;
        bool IsHomeOf(RE::TESObjectCELL* cell, const Arcs::Arc& a, Arcs::Role role) const;
        /** Which of these roles (bit per role) has their home in this interior: the cheap checks first, then one pass
         *  over the cell's beds for all of them at once. 0 = none. */
        std::uint8_t HomeOfCast(RE::TESObjectCELL* cell, const Arcs::Arc& a, std::uint32_t roles) const;
        void Ended(const Arcs::Arc& a);   // memories, the rumor in the hold (lock held)
        void Spread(const Arcs::Arc& a);  // what the hold hears while it is open (lock held)
        void Tell(std::uint32_t id, const std::string& rumor, int replace, RE::FormID asker, RE::FormID place);

        void Reconcile();   // withdraws rumors of arcs this save does not know (lock not held)
        void Pace();        // a town's story when the player has lingered there (main thread, lock not held)

        RE::FormID m_paceTown = 0;       // the town the player is in, and since when (game days)
        float m_paceSince = 0.0f;
        float m_paceTried = -1.0f;       // the last time a story was looked for (game days)

        mutable std::mutex m_mutex;
        Arcs::Book m_book;
        bool m_reconciled = true;                                   // after a load: not yet looked at
        std::chrono::steady_clock::time_point m_loadedAt{};
        std::atomic<int> m_writing{0};                              // rumors being written now
        std::unordered_map<std::uint32_t, Pending> m_pending;
        std::vector<Folk> m_folk;
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_journalAliases;   // actor base -> quests naming it
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_questPlaces;      // location -> quests that put something there

        bool m_catalogued = false;

    public:
        /** The arc natives of script IntelEngine (IntelEngine_Arcs.psc calls them). */
        static void Register(RE::BSScript::IVirtualMachine* a_vm, int& count);
    };
}
