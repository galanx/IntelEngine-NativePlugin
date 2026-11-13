"""Compile real StuckDetector source/header with actor/settings/clock stubs.
Usage: python RunStuckDetectorCheck.py [source-dir] (defaults to SKSE/src).
"""
import pathlib
import subprocess
import sys
import tempfile

src = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1] / 'src'
header = (src / 'StuckDetector.h').read_text(encoding='utf-8').replace('#pragma once', '').replace('#include "Plugin.h"', '').replace('private:', 'public:')
source = (src / 'StuckDetector.cpp').read_text(encoding='utf-8').replace('#include "StuckDetector.h"', '').replace('#include "Settings.h"', '').replace('std::chrono::steady_clock::now()', 'testNow')
with tempfile.TemporaryDirectory(prefix='stuck-detector-') as directory:
    work = pathlib.Path(directory)
    cpp = work / 'check.cpp'
    cpp.write_text('''#include <chrono>
#include <mutex>
#include <cassert>
#include <vector>
#include <iostream>
namespace RE { struct Position { float x{}, y{}, z{}; }; struct Actor { Position p; Position GetPosition() { return p; } }; }
namespace IntelEngine { struct Settings { int stuckMaxChecks = 3, stuckMaxRecovery = 3; static Settings* GetSingleton() { static Settings s; return &s; } }; }
std::chrono::steady_clock::time_point testNow;
''' + header + source + '''
int main() {
    using namespace std::chrono;
    using IntelEngine::StuckDetector;
    auto tick = [](int halfSeconds) { testNow = steady_clock::time_point(seconds(100) + milliseconds(500 * halfSeconds)); };
    RE::Actor actor;
    StuckDetector fast, slow;
    std::vector<int> fastActions, slowActions;
    for (int n = 0; n <= 60; ++n) {
        tick(n);
        int status = fast.CheckStuckStatus(&actor, 0, 1.0f);
        if (status) { assert(n % 6 == 0); fastActions.push_back(n * 10 + status); }
        if (n % 6 == 0) {
            status = slow.CheckStuckStatus(&actor, 0, 1.0f);
            if (status) slowActions.push_back(n * 10 + status);
        }
    }
    assert(fastActions == slowActions);
    assert((fastActions == std::vector<int>{181, 361, 543}));
    StuckDetector moved;
    tick(0); moved.CheckStuckStatus(&actor, 0, 1.0f);
    tick(6); moved.CheckStuckStatus(&actor, 0, 1.0f);
    tick(12); moved.CheckStuckStatus(&actor, 0, 1.0f);
    actor.p.x = 2.0f;
    tick(18); assert(moved.CheckStuckStatus(&actor, 0, 1.0f) == 0);
    assert(moved.m_slots[0].stuckCheckCount == 0);
    tick(24); assert(moved.CheckStuckStatus(&actor, 0, 1.0f) == 0);
    assert(moved.m_slots[0].stuckCheckCount == 1);
    moved.ResetSlot(0, &actor);
    tick(25); assert(moved.CheckStuckStatus(&actor, 0, 1.0f) == 0);
    assert(moved.m_slots[0].stuckCheckCount == 1); // reset cleared the throttle
    tick(600); assert(moved.CheckStuckStatus(&actor, 0, 1.0f) == 0);
    assert(moved.m_slots[0].stuckCheckCount == 2); // long gap is one sample
    moved.ResetSlot(0, nullptr);
    tick(601); assert(moved.CheckStuckStatus(&actor, 0, 1.0f) == 0);
    assert(moved.m_slots[0].stuckCheckCount == 0);
    std::cout << "PASS: 0.5s/3s escalation parity, movement reset, both slot resets, long gap\\n";
}
''', encoding='utf-8')
    exe = work / 'check.exe'
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
