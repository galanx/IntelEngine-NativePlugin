#pragma once

// The game side of the 4.0 living world (LivingWorld.h holds the logic): keeps the record in the co-save,
// turns the player's arrivals into seams for the story engine, asks the director whether a moment may land
// now, and lets the director rank the story engine's candidates. At the Classic level it changes nothing.

#include "LivingWorld.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace IntelEngine {

    class LivingWorldBridge final : public RE::BSTEventSink<RE::TESActorLocationChangeEvent>,
                                    public RE::BSTEventSink<RE::BGSActorCellEvent>,
                                    public RE::BSTEventSink<RE::TESSleepStopEvent> {
    public:
        static LivingWorldBridge* GetSingleton() {
            static LivingWorldBridge instance;
            return &instance;
        }

        /** Listen for the player's arrivals and waking up (kDataLoaded). Safe to call twice. */
        void Register();
        /** kDataLoaded: the places a life can go (inns, dungeons, towns) are read from the game's data once, at the main
         *  menu. It used to be done on the first look after a save loaded, inside a frame of play. */
        void CatalogPlaces();
        /** The towns and cities of the main world with their map markers (armies march off a field toward one). */
        struct TownMark {
            RE::FormID location = 0;
            RE::FormID hold = 0;
            RE::FormID marker = 0;
        };
        std::vector<TownMark> Towns();

        LivingWorld::Settings GetSettings() const;
        /** From the scripts: on load and whenever a living-world setting changes. */
        void SetSettings(const LivingWorld::Settings& settings);
        bool IsClassic() const { return GetSettings().level == LivingWorld::WorldLevel::Classic; }

        /** kPostLoadGame and kNewGame: the place the game is in now is where it begins, no arrival. Until then
         *  (from Revert on) the player's location events are not arrivals: while a save loads the player is first
         *  in no place at all, and the real place then counted as arriving there (2026-11-18). */
        void OnGameLoaded(bool newGame);

        /** May a story moment land now? Seam ids are LivingWorld::Seam values; 0 is the old timer. */
        bool MayDeliver(int seam, bool urgent);

        /** The director's score for one story candidate known from SkyrimNet's history (async thread). */
        float ScoreStoryCandidate(RE::FormID actor, int interactionCount, float daysSinceLastMet, bool sameHold,
                                  bool friendOfFriend, float nowDays, std::uint32_t seed);

        /** A story moment was dispatched with this NPC: count it for pacing and rotation. */
        void OnStoryDispatched(RE::FormID actor, const std::string& storyType);
        /** Game days since the last story moment (since the game began when there was none). */
        float QuietDays() const;
        /** Game days since this NPC was last part of a story moment (very many when never). */
        float DaysSincePicked(RE::FormID actor) const;
        /** The people whose life the record keeps (followers now or left behind), most recent first, at most
         *  `limit`: the scripts hold them in the lives quest's aliases so the game keeps them in memory. A hireling's
         *  reference is not persistent: once they walked into a cell that was unloaded, the game let go of them, their
         *  record froze at "near the player" and their own life never began (Jenassa, 2026-11-17). */
        std::vector<RE::Actor*> Keepers(std::size_t limit) const;

        // ---- lives of followers the player left behind (LivingWorldLives.cpp) ----

        /** What a life change asks of the scripts. One is out at a time. */
        enum class OrderKind : int { None = 0, Move = 1, GoHome = 2, Release = 3, Greet = 4 };

        /** From the scripts every few seconds: see who serves the player, who was left behind and who is on one of
         *  the player's tasks, and plan at most one change. Only at the Lives level do lives begin; below it the
         *  ones under way are ended. */
        void Track();
        /** Test hook: the next order sends this former follower adventuring at the dungeon of the catalogue nearest to
         *  the player that the player cannot see (the meeting at a dungeon is tested from it). The name of the place,
         *  "" when there is no life for them or no such dungeon. */
        std::string DebugAdventure(RE::Actor* actor);
        /** They have just been with the player (a venture's meeting or its end): their own life does not greet the
         *  player again for this step. Jenassa, met at Embershard a minute before, "caught sight of" the player once
         *  more when the venture ended (2026-11-18). */
        void MarkGreeted(RE::FormID actor);
        /** The actor of the next change, or none (the order stays out until OrderDone). */
        RE::Actor* TakeOrder();
        OrderKind CurrentOrderKind() const;
        RE::TESObjectREFR* OrderTarget() const;
        float OrderOffset(int axis) const;
        RE::Actor* OrderCompanion() const;
        bool OrderProtect() const;
        std::string OrderNarration() const;
        /** The scripts carried it out (true) or could not now (false: someone could see, try again later). */
        void OrderDone(bool ok);

        /** For the NPC's own prompt: their life since the player left them, first person. Empty when none. */
        std::string OwnLifeLine(RE::Actor* actor) const;
        /** For the story director's candidate list: the same, third person. */
        std::string DirectorLifeLine(RE::FormID actor) const;
        /** 4.0 quests: a former follower living a life of their own (a venture may be theirs). */
        bool HasOwnLife(RE::FormID actor) const;
        /** Tales: former followers living a life of their own whom a story may give a part: alive, not with the
         *  player, on no errand, in no quest of the journal. Where their life has them now (a location, 0 = their
         *  usual place) and their life in a line. Main thread. */
        /** Close to the player in the game's own terms: once travelled with them, a confidant or more by the game's
         *  relationships, wed to them, or a confidant in SkyrimNet Relationships when it is installed (read softly).
         *  Only such a person crosses a hold for the player on less than an urgent matter. Main thread. */
        bool CloseToPlayer(RE::Actor* actor) const;
        struct OldCompanion {
            RE::FormID actor = 0;
            RE::FormID place = 0;
            std::string life;
        };
        std::vector<OldCompanion> OldCompanions() const;
        /** 4.0 quests: another former follower free to go along on a venture (as NextStep chooses one), 0 = none. */
        RE::FormID FreeCompanionFor(RE::FormID actor) const;
        /** 4.0 quests: whether someone may be taken out on the road with a venture: a grown person, alive, not with
         *  the player or near them, in no scene, no running quest of the journal, no IntelEngine errand, not married
         *  to the player, not keeping a shop. Main thread. */
        static bool FreeToTravel(RE::Actor* actor);
        /** 4.0 quests: where a former follower is on an adventure of their own life right now (the dungeon), and with
         *  whom; {0, 0} when they are not. */
        std::pair<RE::FormID, RE::FormID> AdventuringAt(RE::FormID actor) const;
        /** The hold an actor is in, by name (for world-knowledge conditions). */
        static std::string HoldName(RE::Actor* actor);
        /** How an NPC is tied to the player's quests: 0 not, 1 takes part in a running quest of the journal's kinds,
         *  2 the target of an objective the player has open (any quest, IntelEngine's included). */
        // skipStorylines: leave IntelEngine's own storyline objectives out (someone a request is about may go
        // along with the player to the one who asked: that is the storyline itself, not a move away from it).
        static int QuestTie(RE::Actor* actor, bool skipStorylines = false);
        /** May the story director offer this NPC at all? Never the target of an open objective; nobody in a running
         *  quest when "NPCs of running quests may move" is Never. Below the Lives and Rumors levels: as before 4.0. */
        bool MayOfferForStory(RE::Actor* actor) const;
        /** May this kind of moment move this NPC? With "only for something important", someone in a running quest
         *  comes only to bring a quest (a message always goes by courier, the sender stays). */
        bool MayMoveForStory(RE::Actor* actor, const std::string& storyType, bool skipStorylines = false) const;
        /** The line for the director's candidate list when the setting limits this NPC, else empty. */
        std::string QuestTieLine(RE::Actor* actor) const;
        /** The record of one NPC, for tests and the log. */
        std::string Describe(RE::Actor* actor) const;
        /** For the dashboard: those who follow the player or once did (name, with the player or not, and in a line
         *  where they were parted from and what they have made of their life since). JSON. */
        std::string DashboardJson() const;
        /** A fight, a conversation, a scene, a menu over the game: no ordinary moment lands now. */
        static bool IsBadMoment();
        /** Another mod's scene with the player in it has begun or ended (OStim's thread events, told by the script). */
        static void SetSceneBusy(bool busy);
        /** A dungeon, cave, crypt, camp, lair or den: the place itself, not its parents. */
        static bool IsDungeon(const RE::BGSLocation* location);

        void Save(SKSE::SerializationInterface* intfc);
        void Load(SKSE::SerializationInterface* intfc, std::uint32_t version, std::uint32_t length);
        void Revert();

        RE::BSEventNotifyControl ProcessEvent(const RE::TESActorLocationChangeEvent* event,
                                              RE::BSTEventSource<RE::TESActorLocationChangeEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::BGSActorCellEvent* event,
                                              RE::BSTEventSource<RE::BGSActorCellEvent>*) override;
        RE::BSEventNotifyControl ProcessEvent(const RE::TESSleepStopEvent* event,
                                              RE::BSTEventSource<RE::TESSleepStopEvent>*) override;

    private:
        LivingWorldBridge() = default;

        LivingWorld::Life& Touch(RE::FormID actor);  // m_mutex held
        void SendSeam(LivingWorld::Seam seam);
        void OnPlayerLocation(const RE::BGSLocation* location);
        static float NowDays();

        struct Spot {
            RE::FormID location = 0;
            RE::FormID hold = 0;
            RE::FormID ref = 0;        // where to put someone: an inn's front door inside, a dungeon's or town's marker
            RE::NiPoint3 offset{};     // from that reference to where they stand (an inn: just inside, on the floor)
        };
        struct Order {
            OrderKind kind = OrderKind::None;
            RE::FormID actor = 0;
            RE::FormID companion = 0;
            Spot spot;
            LivingWorld::LifeStep step;
            std::string narration;
        };

        void BuildCatalogue();                                   // once, on the first Track
        const Spot* FindSpot(RE::FormID location) const;
        bool PlanOne(float now, const LivingWorld::Settings& settings, bool writable);  // m_mutex held
        void Commit(const Order& order, float now);             // m_mutex held
        void WriteStep(const Order& order);                     // memories and the rumor, off the game's threads
        void DropRumor(LivingWorld::Life& life);                // m_mutex held
        void Reconcile();                                       // after a load: rumors this save does not know go
        std::string Fill(std::string text, const LivingWorld::Life& life, const std::string& player) const;

        mutable std::mutex m_mutex;
        std::vector<LivingWorld::Life> m_lives;
        LivingWorld::DirectorState m_state;
        LivingWorld::Settings m_settings;
        bool m_settingsKnown = false;
        float m_lastRoadSeamDay = -1.0e6f;
        bool m_registered = false;
        const RE::BGSLocation* m_lastLocation = nullptr;  // m_mutex held
        bool m_lastLocationKnown = false;                  // false after a load: the first place is no arrival
        bool m_loading = false;                            // from Revert until OnGameLoaded: no arrivals

        bool m_catalogued = false;
        std::vector<Spot> m_inns, m_dungeons, m_towns;
        Order m_order;                                     // the one out with the scripts
        std::chrono::steady_clock::time_point m_orderAt{};
        std::chrono::steady_clock::time_point m_loadedAt{};  // SkyrimNet's timeline check runs after a load
        bool m_reconciled = false;
        std::string m_lastFollowers;                        // who followed at the last look, for the log
        // Who serves the player is found by asking every actor the game keeps in its process lists, a slice a look
        // (Track; only that function touches these). A load starts it over (m_scanEpoch, from Revert).
        std::vector<RE::ActorHandle> m_scanHandles;         // the pass under way
        std::size_t m_scanAt = 0;
        std::vector<RE::FormID> m_scanFound;                // teammates found in it so far
        std::vector<RE::FormID> m_servingIds;               // the last pass that was finished
        bool m_servingKnown = false;
        int m_scanCount = 0;                                // how many actors that pass went through
        std::atomic<std::uint32_t> m_scanEpoch{0};
        std::uint32_t m_scanEpochSeen = 0;
        std::unordered_map<RE::FormID, std::string> m_whyNot;  // why someone's own life waits, for the log
    };
}
