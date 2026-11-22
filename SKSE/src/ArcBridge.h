#pragma once

#include "Arcs.h"
#include "FrameSpread.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace IntelEngine {

    // What a quest of the game is called: its title, or, for an errand that has none (it stands under Miscellaneous
    // in the journal with an objective and no name: "Amren's family sword" is one), its first objective, with the
    // places only the running quest can fill left as dots.
    inline std::string QuestLabel(RE::TESQuest* quest) {
        if (!quest) return {};
        if (const char* title = quest->GetFullName(); title && *title) return title;
        for (auto* objective : quest->objectives) {
            const char* text = objective ? objective->displayText.c_str() : nullptr;
            if (!text || !*text) continue;
            std::string label = text;
            for (auto at = label.find('<'); at != std::string::npos; at = label.find('<', at)) {
                const auto end = label.find('>', at);
                if (end == std::string::npos) break;
                label.replace(at, end - at + 1, "...");
                at += 3;
            }
            return label;
        }
        return {};
    }

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
        /** The player is in talk with somebody other than this one: a line said to the player within `within` real
         *  seconds, or one of theirs still on its way or being said. Whoever comes with words of their own waits for
         *  that talk to be over: a request sent into it was given to the other speaker by SkyrimNet (Acolyte Jenssen's,
         *  spoken as an aside of Ulfberth's, 2026-11-22). Any thread. */
        static bool PlayerTalksWithAnother(RE::FormID except, float within);
        /** What the town says of someone's trouble when the story's writer wrote nothing for it (a story asked for as
         *  one whose asker comes in person, who then did not come): the asker's own request as people would have
         *  heard them make it. "Ysolda has trouble, and has been asking around" told nobody anything (2026-11-22). */
        static std::string TroubleTalk(const std::string& asker, const std::string& ask);
        /** No other story came to the player or ended within the hour (the manners every offer keeps). */
        bool QuietNow(std::uint32_t id);
        /** A quest moved a stage on: noted, with the hour. Whether it holds anything back is QuestBusy's to say. */
        void QuestMovedOn(RE::TESQuest* quest);
        /** The player is in the middle of a quest of the journal: one that moved on in the last quarter of an hour
         *  AND that the journal shows (an objective of it is on the player's list). A quest the game runs behind
         *  the player's back holds nothing back. */
        bool QuestBusy(float real);
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
        /** The script laid what is to be looked at outside a place (null: it could not). */
        /** Where a sign of a trail is laid (main thread): a point on the ground out from the place, as x, y, z; empty
         *  for what lies by the place itself, or when no ground is found (then it lies by the place too). */
        std::vector<float> TrailSpot(std::uint32_t id, int clue);
        void CluePlaced(std::uint32_t id, int clue, RE::TESObjectREFR* lies);
        /** The script put what walks by night outside its place (it is night, and the ground there is in the game). */
        void SpiritOut(std::uint32_t id, RE::Actor* shade);
        /** The script took it away again: at first light (how 0: it walks again the next night). */
        void SpiritGone(std::uint32_t id, int how);
        /** The player holds out what lets it rest: the thing leaves their pack and the shade is at rest. False when
         *  it is not out, wants nothing, or the thing is not on the player. */
        bool SpiritRest(std::uint32_t id);
        /** Who or what a story lays outside a place to be looked at (Arcs::Remains): the game's own people and beasts. */
        static RE::TESBoundObject* RemainsBase(std::uint8_t what);
        /** What helps against what lives in a place of that kind (a den's master is a spider or a troll): the game's
         *  own potions and scrolls. Given by the one who is told rightly what killed someone. */
        static RE::TESForm* RemedyFor(std::uint8_t kind, bool spider);
        /** The script placed the company a look asked for. */
        void GangPlaced(std::uint32_t id);
        /** The script placed one of an ambush's men (k = 0 leads). */
        void ThugPlaced(std::uint32_t id, int k, RE::Actor* thug);
        /** The script did what a look asked of the ambush (1 placed them, 2 they struck, 3 called off unseen). */
        void AmbushDone(std::uint32_t id, int what);
        /** Whether a place out in the world is free for a story: no quest of the game still needs it, none of the
         *  player's objectives points into it, no other story or quest of IntelEngine's is set there. */
        bool SiteFree(RE::BGSLocation* location, const std::unordered_set<RE::FormID>& objectiveLocations, std::string& why) const;
        /** A quest of the game that is not finished has something of its own at this place: nothing of IntelEngine's is
         *  set there (a story's master, a bounty's brigand, a thief's hideout). `quest`: its name. Read-only after the
         *  catalogue at the main menu; the game's thread. */
        bool GameNeedsPlace(RE::FormID location, std::string* quest = nullptr) const;
        /** A live story has this place among its own. Takes this bridge's lock. */
        bool PlaceInStory(RE::FormID location) const;
        /** Someone near the asker of a story that began as talk whom they can stop and ask on their way to the player:
         *  alive, awake, at peace, a person, no part of any story, not right beside the player and not out of the
         *  player's sight. Null when nobody is. The game's thread; never called with this bridge's lock held. */
        RE::Actor* Bystander(std::uint32_t id, RE::Actor* asker) const;
        /** Test hook: the asker of this story sets out at the next look. */
        void DebugSetOut(std::uint32_t id);
        /** The "Stories" switch of the settings: off, no new story of the town begins (those open go on to their end). */
        void StoriesOn(bool on);
        /** The story-type switches of the settings that are OFF (kSwitch* bits), as the script last told them. They
         *  governed the dungeon master's one-off moments only; since 2026-11-22 they hold for the same thing inside
         *  a story of the town and for the engine's own quests: no ambush step with "ambush" off, no shadow with
         *  "stalker" off, nobody held with "rescue" off, no master of a place to kill and no bounty with "clear out
         *  enemies" off, no keepsake with "find an item" off, and with "quest" off no story of the town begins. */
        void StorySwitches(std::uint32_t off);
        std::uint32_t SwitchesOff() const { return m_switchesOff.load(); }
        static constexpr std::uint32_t kSwitchAmbush = 1u, kSwitchStalker = 2u, kSwitchRescue = 4u, kSwitchRequests = 8u,
                                       kSwitchClearOut = 16u, kSwitchFindItem = 32u;
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
            bool rumored = false;     // nobody comes to the player with it: it is talk in town, and the player may go to them
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
        /** The offer of a planned story goes out: its asker comes to the player, or stays where they are and waits
         *  (SendOffer: not in front of the story's people, and Arcs::StaysPut). Main thread. */
        void Offer(std::uint32_t id, RE::Actor* asker) { SendOffer(id, asker); }
        /** Is the player among the houses of this town (0: of whichever town the place they stand in belongs to)?
         *  Inside a walled town's own world, or within Arcs::kTownReach of an open town's marker; indoors, wherever
         *  they last stood under the sky. The farms and mills a town's name covers are not it. Main thread. */
        bool PlayerInTownProper(RE::FormID townId = 0);
        bool RequestTale(RE::Actor* asker, const std::string& written = {}, const TaleSeed* seed = nullptr);
        /** Plans a compiled tale (main thread). 0 = refused. */
        std::uint32_t PlanTale(Arcs::Arc a);
        /** The judge's reading of a talk (any thread): which prepared outcome it was, -1 none yet. */
        void Judged(std::uint32_t id, std::uint8_t node, int turn);
        /** Whether a tale is being written now. */
        bool Writing() const { return m_taleBusy.load(); }

        /** For the dashboard: the stories the player has taken on (title, who asked, where, the journal's log so far,
         *  the days left) and how many are on their way. Never what is behind one. JSON. */
        std::string DashboardJson() const;
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
            float nodeAt = -1.0f;        // real seconds: when this step became the story's step
            std::uint8_t spoken = 0;     // a tale's decision: 1 = the talk was read and the decision was not in it (the window asks)
            float visitAt = -1.0f;       // game days: the step's person was last sent to the player
            float stalkHold = -1.0f;     // real seconds: a stalker is not sent after the player again before this
            bool tooFar = false;         // the step's visitor or shadow waits: the player is out of the tale's hold (logged once)
            float offerDue = -1.0f;      // game days: someone on the road from another hold reaches the player then (< 0: not set)
            bool offerSent = false;      // ... and their offer went out
            float deferredAt = -1.0f;    // real seconds: the asker gave up reaching the player (not offered again at once)
            std::uint8_t heldBy = 0;     // why a waiting offer did not go out at the last look (said once per reason)
            bool sought = false;         // the offer went out because the player came to the asker and spoke
            bool setOut = false;         // the asker of a story that began as talk set out to find someone (once)
            bool setOutForced = false;   // test hook: they set out at the next look, whatever the hour and their nature
            bool awaits = false;         // held back only because the player was out of town: they come once the player is back in it
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
        /** A tale's decision step, each look (lock held): once the player and the step's person have talked and gone
         *  quiet, the talk is read for a decision the player has already spoken. */
        void ChoiceTalk(std::uint32_t id, const Arcs::Arc& a, Pending& pending, const Arcs::Facts& f);
        /** That reading is still owed: the window for the decision waits for it. */
        bool ChoiceUnread(const Arcs::Arc& a, const Pending& pending) const;
        /** A tale's ambush step, each look (lock held): when its men are placed, step out, or are called off. */
        void AmbushLook(std::uint32_t id, Arcs::Arc& a, Pending& pending, const Arcs::Node& n, float now, int placed);
        /** The leader's word and what the player says to it (lock held): true when it is over and they fall on the
         *  player, with why. A talk in between is read like a talk step's (SendJudge). */
        bool Parley(std::uint32_t id, const Arcs::Arc& a, Pending& pending, const Arcs::Node& n, float nearest, std::string& why);
        /** RequestTale's work, a part a frame (FrameSpread.h): `answer` is written before its first pause and never
         *  after; the asker's seed is its own copy. */
        FrameJob GatherTale(RE::Actor* asker, std::string written, std::optional<TaleSeed> seed, bool* answer);   // main thread
        void SendTale(std::shared_ptr<void> request, int attempt, std::string fix, std::string previous);   // any thread
        void TaleWritten(std::shared_ptr<void> request, int attempt, const std::string& text);              // main thread
        void SendOffer(std::uint32_t id, RE::Actor* asker);
        /** Why this story's asker does not go to the player now (Arcs::StaysPut), and whether they know the player. */
        Arcs::Stays WhyStays(std::uint32_t id, RE::Actor* asker, bool& knowsPlayer);
        /** They stay where they are: the story waits for the two to meet, and their trouble is talk in town. */
        void OfferWaits(std::uint32_t id, Arcs::Stays why, bool knowsPlayer);
        /** decision: the outcomes are the answers of a decision step, and the question is whether the player has
         *  already said one of them. window: how far back the talk is read, in real seconds (0: the usual five minutes). */
        void SendJudge(std::uint32_t id, std::uint8_t node, RE::FormID who, std::string goal, std::array<std::string, 3> turns,
                       bool force, bool decision = false, float window = 0.0f);
        /** The game's own small quests (favours with a name) of each person, by base: for the tale author only. */
        std::unordered_map<RE::FormID, std::vector<RE::FormID>> m_favours;
        std::atomic<bool> m_taleBusy{false};
        std::atomic<bool> m_storiesOn{true};   // the "Stories" switch, as the script last told it
        std::atomic<std::uint32_t> m_switchesOff{0};   // the story-type switches that are off (kSwitch* bits)
        // The quests that moved a stage on lately (the newest few), each with the real second its hold would end.
        struct QuestMove {
            RE::FormID quest = 0;
            float until = -1.0f;
            bool told = false;   // its line is in the log
        };
        std::mutex m_questMutex;
        std::array<QuestMove, 6> m_questMoves{};
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

        std::atomic<RE::FormID> m_outdoorTown{0xFFFFFFFFu};   // the town whose houses the player last stood among under the sky (0 none, all bits: not seen yet)
        std::unordered_map<RE::FormID, bool> m_walled;        // towns with a world of their own behind walls (main thread)
        RE::FormID m_paceTown = 0;       // the town the player is in, and since when (game days)
        float m_paceSince = 0.0f;
        float m_paceTried = -1.0f;       // the last time a story was looked for (game days)
        double m_pacePlayedAt = 0.0;     // time played (LivingWorldBridge::PlayedSeconds) when the player came to this town
        double m_paceBegunPlayed = -1.0; // and when the last story began by itself; below 0: none this session
        int m_paceHeld = 0;              // why the pacer last held back (so the log says it once)

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
