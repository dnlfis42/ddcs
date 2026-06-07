#include "ddcs/common/clock.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace ddcs::common {

using namespace std::chrono_literals;

TEST(ClockTest, ReturnsMonotonicTimeFromSteadyClock) {
    SteadyClock clock;
    auto const t1 = clock.now();
    auto const t2 = clock.now();
    EXPECT_GE(t2, t1);
}

TEST(ClockTest, StartsManualClockAtDefaultTime) {
    ManualClock clock;
    EXPECT_EQ(clock.now(), Clock::time_point{});
}

TEST(ClockTest, AdvancesManualClockByDuration) {
    ManualClock clock;
    auto const t0 = clock.now();

    clock.advance(1s);
    EXPECT_EQ(clock.now() - t0, std::chrono::duration_cast<Clock::duration>(1s));

    clock.advance(500ms);
    EXPECT_EQ(clock.now() - t0, std::chrono::duration_cast<Clock::duration>(1500ms));
}

TEST(ClockTest, SetsManualClockTime) {
    ManualClock clock;
    Clock::time_point const t{1234ms};
    clock.set(t);
    EXPECT_EQ(clock.now(), t);
}

TEST(ClockTest, DispatchesNowThroughClockInterface) {
    ManualClock impl;
    Clock& clock = impl;

    EXPECT_EQ(clock.now(), Clock::time_point{});

    impl.advance(2s);
    EXPECT_EQ(clock.now(), Clock::time_point{} + std::chrono::duration_cast<Clock::duration>(2s));
}

} // namespace ddcs::common
