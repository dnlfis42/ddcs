#include "ddcs/ctrl/detail/fixed_rate_schedule.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_scheduler.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

using ddcs::common::Clock;
using ddcs::common::ManualClock;
using ddcs::ctrl::detail::FixedRateSchedule;

TEST(FixedRateScheduleTest, RejectsNonpositiveIntervals) {
    EXPECT_THROW(FixedRateSchedule{0ns}, std::invalid_argument);
    EXPECT_THROW(FixedRateSchedule{-1ns}, std::invalid_argument);
}

TEST(FixedRateScheduleTest, StartsOneIntervalAfterTheOrigin) {
    ManualClock clock{Clock::time_point{17s}};
    FixedRateSchedule schedule{1s};
    schedule.start(clock.now());
    EXPECT_EQ(schedule.deadline(), Clock::time_point{18s});
    EXPECT_EQ(schedule.lateness(clock.now()), 0ns);
}

TEST(FixedRateScheduleTest, KeepsTheGridAcrossRepeatedWork) {
    ManualClock clock;
    FixedRateSchedule schedule{1s};
    schedule.start(clock.now());
    for (int tick = 1; tick <= 100; ++tick) {
        clock.set(Clock::time_point{std::chrono::seconds{tick}});
        EXPECT_EQ(schedule.deadline(), clock.now());
        EXPECT_EQ(schedule.lateness(clock.now()), 0ns);
        clock.advance(34ms);
        EXPECT_EQ(schedule.advance(clock.now()), 0u);
    }
    EXPECT_EQ(schedule.deadline(), Clock::time_point{101s});
}

TEST(FixedRateScheduleTest, DelayedDispatchDoesNotShiftTheNextDeadline) {
    ManualClock clock;
    FixedRateSchedule schedule{1s};
    schedule.start(clock.now());
    clock.advance(1200ms);
    EXPECT_EQ(schedule.lateness(clock.now()), 200ms);
    clock.advance(100ms);
    EXPECT_EQ(schedule.advance(clock.now()), 0u);
    EXPECT_EQ(schedule.deadline(), Clock::time_point{2s});
}

TEST(FixedRateScheduleTest, SkipsElapsedDeadlinesIncludingExactBoundaries) {
    struct Case {
        std::chrono::nanoseconds finished;
        std::uint64_t skipped;
        std::chrono::nanoseconds next;
    };
    for (auto const& c : {
             Case{2s - 1ns, 0, 2s},
             Case{2s, 1, 3s},
             Case{2400ms, 1, 3s},
             Case{5400ms, 4, 6s},
             Case{std::chrono::hours{24}, 86'399, 86'401s},
         }) {
        SCOPED_TRACE(c.finished.count());
        ManualClock clock;
        FixedRateSchedule schedule{1s};
        schedule.start(clock.now());
        clock.advance(c.finished);
        EXPECT_EQ(schedule.advance(clock.now()), c.skipped);
        EXPECT_EQ(schedule.deadline(), Clock::time_point{c.next});
        EXPECT_GT(schedule.deadline(), clock.now());
    }
}

TEST(FixedRateScheduleTest, HonorsConfiguredSubsecondIntervals) {
    ManualClock clock;
    FixedRateSchedule schedule{10ms};
    schedule.start(clock.now());
    clock.advance(35ms);
    EXPECT_EQ(schedule.advance(clock.now()), 2u);
    EXPECT_EQ(schedule.deadline(), Clock::time_point{40ms});
}

TEST(FixedRateScheduleTest, TimerDispatchRunsOnceAfterMultipleMissedPeriods) {
    ManualClock clock;
    ddcs::io::Reactor reactor;
    ddcs::io::TimerScheduler timers{reactor, clock};
    class Sweep final : public ddcs::io::TimerHandler {
    public:
        Sweep(ManualClock& clock_ref, ddcs::io::TimerScheduler& timers_ref)
            : clock(clock_ref),
              timers(timers_ref) {
            schedule.start(clock.now());
            arm();
        }
        void on_expired(ddcs::io::TimerToken) override {
            starts.push_back(clock.now());
            clock.advance(100ms);
            skipped += schedule.advance(clock.now());
            arm();
        }
        void arm() {
            (void)timers.schedule_at(schedule.deadline(), *this);
        }
        ManualClock& clock;
        ddcs::io::TimerScheduler& timers;
        FixedRateSchedule schedule{1s};
        std::vector<Clock::time_point> starts;
        std::uint64_t skipped{};
    } sweep{clock, timers};

    timers.start();
    clock.advance(5400ms);
    timers.dispatch_expired();
    ASSERT_EQ(sweep.starts.size(), 1u);
    EXPECT_EQ(sweep.skipped, 4u);
    EXPECT_EQ(sweep.schedule.deadline(), Clock::time_point{6s});
    timers.dispatch_expired();
    EXPECT_EQ(sweep.starts.size(), 1u);
    clock.set(Clock::time_point{6s});
    timers.dispatch_expired();
    ASSERT_EQ(sweep.starts.size(), 2u);
    EXPECT_EQ(sweep.starts.back(), Clock::time_point{6s});
    EXPECT_EQ(sweep.schedule.deadline(), Clock::time_point{7s});
}

} // namespace
