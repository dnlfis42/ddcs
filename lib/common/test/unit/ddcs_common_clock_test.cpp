#include "ddcs/common/clock.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace ddcs::common {

using namespace std::chrono_literals;

TEST(Clock, SystemClockMonotonic) {
    SystemClock c;
    auto const t1 = c.now();
    auto const t2 = c.now();
    EXPECT_GE(t2, t1);
}

TEST(Clock, ManualClockDefaultStart) {
    ManualClock c;
    EXPECT_EQ(c.now(), Clock::time_point{});
}

TEST(Clock, ManualClockAdvance) {
    ManualClock c;
    auto const t0 = c.now();

    c.advance(1s);
    EXPECT_EQ(c.now() - t0, std::chrono::duration_cast<Clock::duration>(1s));

    c.advance(500ms);
    EXPECT_EQ(c.now() - t0, std::chrono::duration_cast<Clock::duration>(1500ms));
}

TEST(Clock, ManualClockSet) {
    ManualClock c;
    Clock::time_point const t{1234ms};
    c.set(t);
    EXPECT_EQ(c.now(), t);
}

TEST(Clock, PolymorphicUsage) {
    ManualClock impl;
    Clock& c = impl;

    EXPECT_EQ(c.now(), Clock::time_point{});

    impl.advance(2s);
    EXPECT_EQ(c.now(), Clock::time_point{} + std::chrono::duration_cast<Clock::duration>(2s));
}

} // namespace ddcs::common
