"""Compile the real dashboard hotkey loop against Win32/SKSE stubs; requires clang++.

Usage: python SKSE/tests/RunDashboardHotkeyCheck.py
"""
import pathlib
import subprocess
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / "src" / "DashboardUIManager.cpp"
source = src.read_text(encoding="utf-8")
start = source.index("    void DashboardUIManager::HotkeyPollLoop() {")
end = source.index("\n    }", start) + len("\n    }")
loop = source[start:end].replace(
    "std::this_thread::sleep_for(kPollIntervalMs);", "testSleepFor();"
).replace("std::chrono::steady_clock::now()", "FakeClock::now()")

with tempfile.TemporaryDirectory(prefix="dashboard-hotkey-") as directory:
    work = pathlib.Path(directory)
    cpp = work / "check.cpp"
    cpp.write_text('''#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

using HWND = void*;
using DWORD = unsigned long;
constexpr int VK_CONTROL = 0x11, VK_SHIFT = 0x10, VK_MENU = 0x12;
constexpr int kModCtrl = 1, kModShift = 2, kModAlt = 4;
constexpr auto kPollIntervalMs = std::chrono::milliseconds(50);
constexpr auto kKeyPressCooldownMs = std::chrono::milliseconds(300);
struct FakeClock {
    using duration = std::chrono::milliseconds;
    using time_point = std::chrono::time_point<FakeClock, duration>;
    static constexpr bool is_steady = true;
    static time_point current;
    static time_point now() { return current; }
};
FakeClock::time_point FakeClock::current{};
struct Frame { DWORD foreground = 42; bool paused = false; std::array<bool, 256> keys{}; };
std::vector<Frame> frames;
size_t frameIndex = 0;
Frame active;
class DashboardUIManager;
DashboardUIManager* currentManager = nullptr;
void testSleepFor();
HWND GetForegroundWindow() { return active.foreground ? reinterpret_cast<HWND>(1) : nullptr; }
DWORD GetWindowThreadProcessId(HWND, DWORD* processId) { *processId = active.foreground; return 1; }
DWORD GetCurrentProcessId() { return 42; }
short GetAsyncKeyState(int key) { return active.keys.at(static_cast<size_t>(key)) ? short(0x8000) : 0; }
class DashboardConfig {
public:
    int hotkey = 55, modifiers = kModShift;
    static DashboardConfig* GetSingleton() { static DashboardConfig config; return &config; }
    int GetHotkey() const { return hotkey; }
    int GetModifiers() const { return modifiers; }
};
namespace RE {
class UI {
public:
    static UI* GetSingleton() { static UI ui; return &ui; }
    bool GameIsPaused() const { return active.paused; }
};
}
class DashboardUIManager {
public:
    std::atomic<bool> stopPolling_{false}, isOpen_{false};
    int toggles = 0;
    static DashboardUIManager* GetSingleton() { return currentManager; }
    void Toggle() { ++toggles; }
    void HotkeyPollLoop();
};
namespace SKSE {
class TaskInterface {
public:
    template<class F> void AddTask(F&& task) { task(); }
};
inline TaskInterface* GetTaskInterface() { static TaskInterface task; return &task; }
}
void testSleepFor() {
    assert(frameIndex < frames.size());
    active = frames[frameIndex++];
    FakeClock::current += kPollIntervalMs;
    if (frameIndex == frames.size()) currentManager->stopPolling_.store(true);
}
''' + loop + '''
Frame makeFrame(bool key, bool shift = true, bool ctrl = false, bool alt = false,
                DWORD foreground = 42, bool paused = false) {
    Frame frame;
    frame.foreground = foreground;
    frame.paused = paused;
    frame.keys[55] = key;
    frame.keys[VK_SHIFT] = shift;
    frame.keys[VK_CONTROL] = ctrl;
    frame.keys[VK_MENU] = alt;
    return frame;
}
int run(std::vector<Frame> input) {
    frames = std::move(input);
    frames.insert(frames.begin(), 7, makeFrame(false));
    frameIndex = 0;
    FakeClock::current = FakeClock::time_point(std::chrono::milliseconds(-100000));
    DashboardUIManager manager;
    currentManager = &manager;
    manager.HotkeyPollLoop();
    return manager.toggles;
}
int main() {
    for (int configured = 0; configured < 8; ++configured) {
        DashboardConfig::GetSingleton()->modifiers = configured;
        for (int held = 0; held < 8; ++held) {
            assert(run({makeFrame(true, held & kModShift, held & kModCtrl, held & kModAlt)})
                   == (held == configured ? 1 : 0));
        }
    }
    DashboardConfig::GetSingleton()->modifiers = kModShift;
    assert(run({
        makeFrame(true, false), makeFrame(true), makeFrame(true, true, true),
        makeFrame(true, true, false, true), makeFrame(false),
        makeFrame(false), makeFrame(false), makeFrame(false), makeFrame(false),
        makeFrame(false), makeFrame(false), makeFrame(true)
    }) == 2); // exact Shift+7; unexpected modifiers rejected; fresh edge toggles
    assert(run({makeFrame(true, true, false, false, 99)}) == 0);
    assert(run({makeFrame(true, true, false, false, 0)}) == 0);
    assert(run({
        makeFrame(true, true, false, false, 99), makeFrame(true)
    }) == 0); // returning focus while held is not a new press
    assert(run({
        makeFrame(true, true, false, false, 99), makeFrame(true),
        makeFrame(false), makeFrame(true)
    }) == 1); // held through focus return is suppressed until released
    assert(run({
        makeFrame(true), makeFrame(false), makeFrame(true), makeFrame(false),
        makeFrame(false), makeFrame(false), makeFrame(false), makeFrame(false),
        makeFrame(false), makeFrame(false), makeFrame(true)
    }) == 2); // cooldown blocks a rapid re-press, then permits a later one
    assert(run({makeFrame(true, true, false, false, 42, true)}) == 0);
    assert(run({makeFrame(true, true, false, false, 42, true), makeFrame(true)}) == 1);
    std::cout << "PASS: exact modifiers, foreground/focus transitions, pause, and cooldown\\n";
}
''', encoding="utf-8")
    exe = work / "check.exe"
    subprocess.run(["clang++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
