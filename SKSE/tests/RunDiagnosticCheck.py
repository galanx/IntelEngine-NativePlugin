"""Run production distance/tracker logic against isolated engine boundaries.
Usage: python RunDiagnosticCheck.py <vcpkg-triplet-dir>
"""
import pathlib
import subprocess
import sys
import tempfile

src = pathlib.Path(__file__).resolve().parents[1] / 'src'
header = (src / 'OffScreenTracker.h').read_text().replace('#include "Plugin.h"', '')
tracker = (src / 'OffScreenTracker.cpp').read_text().replace('#include "OffScreenTracker.h"', '')
papyrus = (src / 'Papyrus.cpp').read_text(encoding='utf-8')
start = papyrus.index('    RE::BSFixedString DescribeDistance(RE::StaticFunctionTag*, RE::TESObjectREFR* a,')
helper = papyrus[start:papyrus.index('\n    }', start) + 6]
start = papyrus.index('    RE::BSFixedString GetDirectorFailureMessage(RE::StaticFunctionTag*, RE::BSFixedString name)')
director = papyrus[start:papyrus.index('\n    }', start) + 6]
with tempfile.TemporaryDirectory(prefix='ie-diag-') as directory:
    cpp = pathlib.Path(directory) / 'check.cpp'
    cpp.write_text('''#include <cassert>
#include <cmath>
#include <mutex>
#include <string>
#include <iostream>
#define FMT_HEADER_ONLY
#include <fmt/format.h>
namespace logger { template<class... T> void debug(T&&...) {} }
namespace RE {
using BSFixedString = std::string;
struct StaticFunctionTag {};
struct Point { float x{}, y{}, z{}; float GetDistance(Point p) const { return std::sqrt((x-p.x)*(x-p.x)+(y-p.y)*(y-p.y)+(z-p.z)*(z-p.z)); } };
struct Cell { bool interior; bool IsInteriorCell() const { return interior; } };
struct TESObjectREFR { Cell* cell; void* world; Point p; Cell* GetParentCell() { return cell; } void* GetWorldspace() { return world; } Point GetPosition() { return p; } };
}
''' + header + tracker + helper + '''
struct NPCIndex {
    static inline std::string reason;
    static NPCIndex* GetSingleton() { static NPCIndex index; return &index; }
    void ResolveDirectorActor(const char*, std::string* rejection) { *rejection = reason; }
};
''' + director + '''
int main() {
    NPCIndex::reason = "lookup-null";
    assert(GetDirectorFailureMessage(nullptr, "Marcurio").find("Marcurio isn't loaded right now") == 0);
    for (const auto* reason : {"not-found", "ambiguous", "dead", "disabled", "deleted", "id-mismatch", ""}) {
        NPCIndex::reason = reason;
        assert(GetDirectorFailureMessage(nullptr, "Marcurio").find("isn't loaded") == std::string::npos);
    }
    RE::Cell interior{true}, otherInterior{true}, exterior{false}, otherExterior{false};
    int world{}, otherWorld{};
    RE::TESObjectREFR a{&interior, nullptr, {}}, b{&interior, nullptr, {3,4,12}};
    assert(DescribeDistance(nullptr, &a, &b) == "dist=13 sameSpace=yes");
    b.cell = &otherInterior; assert(DescribeDistance(nullptr, &a, &b) == "sameSpace=no");
    a.cell = &exterior; b.cell = &otherExterior; a.world = b.world = &world;
    assert(DescribeDistance(nullptr, &a, &b) == "dist=13 sameSpace=yes");
    b.world = &otherWorld; assert(DescribeDistance(nullptr, &a, &b) == "sameSpace=no");
    b.world = nullptr; assert(DescribeDistance(nullptr, &a, &b) == "sameSpace=no");
    b.cell = &interior; b.world = &world; assert(DescribeDistance(nullptr, &a, &b) == "sameSpace=no");
    assert(DescribeDistance(nullptr, nullptr, &b) == "sameSpace=no");
    a.cell = nullptr; b.cell = &exterior;
    assert(DescribeDistance(nullptr, &a, &b) == "dist=13 sameSpace=yes");
    b.cell = nullptr; assert(DescribeDistance(nullptr, &a, &b) == "dist=13 sameSpace=yes");
    b.world = &otherWorld; assert(DescribeDistance(nullptr, &a, &b) == "sameSpace=no");
    b.world = nullptr; assert(DescribeDistance(nullptr, &a, &b) == "sameSpace=no");
    b.cell = &interior; b.world = &world; assert(DescribeDistance(nullptr, &a, &b) == "sameSpace=no");
    auto* tracker = IntelEngine::OffScreenTracker::GetSingleton();
    float movement; int ticks;
    tracker->InitSlot(0, 10, 0, 0);
    assert(tracker->CheckProgress(0, 9, 100, 0, false, movement, ticks) == 0 && movement == -1 && ticks == 0);
    assert(tracker->CheckProgress(0, 10, 100, 0, true, movement, ticks) == 0 && movement == 0 && ticks == 1);
    assert(tracker->CheckProgress(0, 10, 150, 0, true, movement, ticks) == 0 && movement == 50 && ticks == 0);
    for (int n = 1; n <= 3; ++n) assert(tracker->CheckProgress(0, 10, 150, 0, true, movement, ticks) == (n == 3) && ticks == n);
    tracker->InitSlot(0, 10, 0, 0);
    assert(tracker->CheckProgress(0, 10, 217, 0, false, movement, ticks) == 1 && movement == 217);
    tracker->ResetSlot(0);
    assert(tracker->CheckProgress(0, 10, 150, 0, false, movement, ticks) == 0 && movement == -1);
    assert(tracker->CheckProgress(-1, 10, 0, 0, false, movement, ticks) == 0 && movement == -1);
    std::cout << "PASS: production distance space/null/3D cases; tracker phase/movement/stationary/reset boundaries\\n";
}
''', encoding='utf-8')
    exe = pathlib.Path(directory) / 'check.exe'
    subprocess.run(['clang++', '-std=c++20', '-I' + str(pathlib.Path(sys.argv[1]) / 'include'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
