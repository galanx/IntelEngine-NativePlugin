#pragma once

#include <algorithm>
#include <chrono>

// How long a piece of work inside the game's frame takes, measured where it runs: said at once when one run is long
// enough to notice, and the slowest of every `every` runs now and then, so a hitch on a big load order shows in the
// log instead of being guessed at. (The rule: nothing of IntelEngine's takes more than about half a millisecond of
// one frame of play.) The counters belong to the caller, one per piece of work, and are touched under the caller's
// own lock: make the clock after taking it, with the time from before it, so that waiting for the lock counts too.
namespace IntelEngine {

    struct FrameStats {
        int runs = 0;
        double slowest = 0.0;
    };

    class FrameClock {
    public:
        using Time = std::chrono::steady_clock::time_point;
        static Time Now() { return std::chrono::steady_clock::now(); }

        FrameClock(const char* what, FrameStats& stats, Time began, int every = 150, double noticeMs = 0.5)
            : m_what(what), m_stats(stats), m_began(began), m_every(every), m_noticeMs(noticeMs) {}
        FrameClock(const FrameClock&) = delete;
        FrameClock& operator=(const FrameClock&) = delete;

        ~FrameClock() {
            const double ms = std::chrono::duration<double, std::milli>(Now() - m_began).count();
            if (ms >= m_noticeMs) logger::info("{} took {:.2f} ms of the game's frame", m_what, ms);
            m_stats.slowest = std::max(m_stats.slowest, ms);
            if (++m_stats.runs >= m_every) {
                logger::info("{}: at most {:.2f} ms of the game's frame in the last {} runs", m_what, m_stats.slowest, m_stats.runs);
                m_stats = FrameStats{};
            }
        }

    private:
        const char* m_what;
        FrameStats& m_stats;
        Time m_began;
        int m_every;
        double m_noticeMs;
    };
}
