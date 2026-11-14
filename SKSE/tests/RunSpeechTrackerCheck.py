"""Compile the real SpeechTracker source/header with event and logger stubs and check its state rules.
Usage: python RunSpeechTrackerCheck.py <triplet dir with nlohmann/json.hpp> [source-dir] (defaults to SKSE/src).
"""
import pathlib
import subprocess
import sys
import tempfile

deps = pathlib.Path(sys.argv[1])
src = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path(__file__).resolve().parents[1] / 'src'
header = (src / 'SpeechTracker.h').read_text(encoding='utf-8').replace('#pragma once', '').replace('#include "Plugin.h"', '')
source = (src / 'SpeechTracker.cpp').read_text(encoding='utf-8').replace('#include "SpeechTracker.h"', '')
with tempfile.TemporaryDirectory(prefix='speech-tracker-') as directory:
    work = pathlib.Path(directory)
    cpp = work / 'check.cpp'
    cpp.write_text('''#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
namespace RE {
using FormID = std::uint32_t;
enum class BSEventNotifyControl { kContinue };
template <class T> struct BSTEventSource { void AddEventSink(void*) {} };
template <class T> struct BSTEventSink {
    virtual ~BSTEventSink() = default;
    virtual BSEventNotifyControl ProcessEvent(const T*, BSTEventSource<T>*) = 0;
};
struct TESForm { FormID id = 0; FormID GetFormID() const { return id; } };
}
namespace SKSE {
struct Text { std::string s; const char* c_str() const { return s.c_str(); } };
struct ModCallbackEvent { Text eventName, strArg; float numArg = 0; RE::TESForm* sender = nullptr; };
inline RE::BSTEventSource<ModCallbackEvent>* GetModCallbackEventSource() { static RE::BSTEventSource<ModCallbackEvent> s; return &s; }
}
namespace logger { template <class... A> void info(A&&...) {} template <class... A> void warn(A&&...) {} }
''' + header + source + '''
int main() {
    using namespace std::chrono;
    using IntelEngine::SpeechTracker;
    using Kind = SpeechTracker::Kind;
    auto* t = SpeechTracker::GetSingleton();
    const auto t0 = SpeechTracker::Clock::time_point(seconds(1000));
    auto at = [&](double s) { return t0 + duration_cast<SpeechTracker::Clock::duration>(duration<double>(s)); };
    const RE::FormID npc = 0x1A66B, other = 0x1A66C;

    // Nobody known: not pending, never spoke.
    assert(!t->IsSpeechPendingAt(npc, at(0)));
    assert(t->SecondsSinceSpeechEndedAt(npc, at(0)) < 0.0f);

    // A line of two segments: pending from the first queue notice to the last end notice, the gap between
    // the segments included (the second is still queued while the first plays and after it ends).
    t->Apply(npc, Kind::Queued, at(0.0));
    assert(t->IsSpeechPendingAt(npc, at(0.1)));          // before any audio: the engine would say "not talking"
    t->Apply(npc, Kind::Queued, at(0.5));
    t->Apply(npc, Kind::AudioStarted, at(2.0));
    assert(t->IsSpeechPendingAt(npc, at(3.0)));
    t->Apply(npc, Kind::AudioEnded, at(5.0));
    assert(t->IsSpeechPendingAt(npc, at(5.1)));          // the second segment has not played yet
    t->Apply(npc, Kind::AudioStarted, at(5.3));
    t->Apply(npc, Kind::AudioEnded, at(8.0));
    assert(!t->IsSpeechPendingAt(npc, at(8.1)));
    assert(t->SecondsSinceSpeechEndedAt(npc, at(10.0)) > 1.9f && t->SecondsSinceSpeechEndedAt(npc, at(10.0)) < 2.1f);
    assert(!t->IsSpeechPendingAt(other, at(3.0)));       // per actor

    // A segment that was queued and never played (purged) stops counting after the grace time, and does not
    // add up with the next line's segments.
    t->Apply(npc, Kind::Queued, at(100.0));
    assert(t->IsSpeechPendingAt(npc, at(100.0 + SpeechTracker::kQueuedGraceSeconds - 1.0)));
    assert(!t->IsSpeechPendingAt(npc, at(100.0 + SpeechTracker::kQueuedGraceSeconds + 1.0)));
    t->Apply(npc, Kind::Queued, at(200.0));
    t->Apply(npc, Kind::AudioStarted, at(201.0));
    t->Apply(npc, Kind::AudioEnded, at(203.0));
    assert(!t->IsSpeechPendingAt(npc, at(203.1)));       // the stale segment of t=100 was dropped, not counted

    // An end notice that never comes does not keep them "speaking" for ever.
    t->Apply(other, Kind::Queued, at(300.0));
    t->Apply(other, Kind::AudioStarted, at(301.0));
    assert(t->IsSpeechPendingAt(other, at(301.0 + SpeechTracker::kAudibleMaxSeconds - 1.0)));
    assert(!t->IsSpeechPendingAt(other, at(301.0 + SpeechTracker::kAudibleMaxSeconds + 1.0)));

    // Audio without a queue notice (a line registered another way) still counts while it plays.
    t->Reset();
    assert(t->SecondsSinceSpeechEndedAt(npc, at(400.0)) < 0.0f);   // a load forgets
    t->Apply(npc, Kind::AudioStarted, at(400.0));
    assert(t->IsSpeechPendingAt(npc, at(401.0)));
    t->Apply(npc, Kind::AudioEnded, at(402.0));
    assert(!t->IsSpeechPendingAt(npc, at(402.1)));
    t->Apply(0, Kind::Queued, at(403.0));                 // no actor: ignored

    // Event names and the narration flag.
    Kind k{};
    assert(SpeechTracker::KindOf("SkyrimNet_SpeechStarted", k) && k == Kind::Queued);
    assert(SpeechTracker::KindOf("SkyrimNet_AudioStarted", k) && k == Kind::AudioStarted);
    assert(SpeechTracker::KindOf("SkyrimNet_AudioEnded", k) && k == Kind::AudioEnded);
    assert(SpeechTracker::KindOf("skyrimnet_audiostarted", k) && k == Kind::AudioStarted);   // the string pool's spelling
    assert(SpeechTracker::KindOf("SKYRIMNET_SPEECHSTARTED", k) && k == Kind::Queued);
    assert(!SpeechTracker::KindOf("SkyrimNet_AudioEndedX", k) && !SpeechTracker::KindOf("SkyrimNet_AudioEnde", k));
    assert(!SpeechTracker::KindOf("SkyrimNet_SpeechComplete", k));
    assert(!SpeechTracker::KindOf("IntelEngine_DashboardOpened", k));
    assert(!SpeechTracker::KindOf(nullptr, k));
    assert(SpeechTracker::IsNarration(R"({"speakerFormId":1,"isNarration":true})"));
    assert(!SpeechTracker::IsNarration(R"({"speakerFormId":1,"isNarration":false})"));
    assert(!SpeechTracker::IsNarration(R"({"speakerFormId":1})"));
    assert(!SpeechTracker::IsNarration("not json"));
    assert(!SpeechTracker::IsNarration(""));
    assert(!SpeechTracker::IsNarration(nullptr));

    // Through the sink: the sender is the speaker; narration and events without a sender change nothing.
    t->Reset();
    RE::TESForm form; form.id = npc;
    SKSE::ModCallbackEvent ev; ev.eventName.s = "SkyrimNet_SpeechStarted"; ev.strArg.s = R"({"isNarration":false})"; ev.sender = &form;
    assert(!t->HasSeenEvents());
    t->ProcessEvent(&ev, nullptr);
    assert(t->HasSeenEvents());
    assert(t->IsSpeechPending(npc));
    t->Reset();
    ev.strArg.s = R"({"isNarration":true})";
    t->ProcessEvent(&ev, nullptr);
    assert(!t->IsSpeechPending(npc));
    ev.strArg.s = "{}"; ev.sender = nullptr;
    t->ProcessEvent(&ev, nullptr);
    assert(!t->IsSpeechPending(npc));
    t->ProcessEvent(nullptr, nullptr);

    // The player's own lines: one time for everybody, forgotten at a load.
    assert(t->SecondsSincePlayerSpokeAt(at(500.0)) < 0.0f);
    t->NotePlayerLine(at(500.0));
    assert(t->SecondsSincePlayerSpokeAt(at(512.0)) > 11.9f && t->SecondsSincePlayerSpokeAt(at(512.0)) < 12.1f);
    t->NotePlayerLine(at(520.0));
    assert(t->SecondsSincePlayerSpokeAt(at(521.0)) < 1.1f);
    t->Reset();
    assert(t->SecondsSincePlayerSpokeAt(at(530.0)) < 0.0f);
    assert(SpeechTracker::IsPlayerLineType("dialogue_player_text") && SpeechTracker::IsPlayerLineType("dialogue_player_stt"));
    assert(SpeechTracker::IsPlayerLineType("dialogue_player") && SpeechTracker::IsPlayerLineType("Dialogue_Player_STT"));
    assert(!SpeechTracker::IsPlayerLineType("dialogue") && !SpeechTracker::IsPlayerLineType("dialogue_player_telepathy"));
    assert(!SpeechTracker::IsPlayerLineType(nullptr));
    std::cout << "PASS: pending across segments, grace for unplayed and unended lines, per actor, reset, event names, narration, sink, the player's lines\\n";
}
''', encoding='utf-8')
    exe = work / 'check'
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-I', str(deps / 'include'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
