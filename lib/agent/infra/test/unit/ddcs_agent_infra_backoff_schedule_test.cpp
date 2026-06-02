#include "ddcs/agent/infra/backoff_schedule.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

using ddcs::agent::infra::BackoffSchedule;
using namespace std::chrono_literals;

// jitter 가 있으니 +/-25% 범위 확인.
void expect_within_jitter(std::chrono::nanoseconds actual, std::chrono::nanoseconds base) {
    auto const lower = base - base * 25 / 100;
    auto const upper = base + base * 25 / 100;
    EXPECT_GE(actual, lower);
    EXPECT_LE(actual, upper);
}

TEST(AgentBackoffScheduleTest, FirstDelayAroundOneSecond) {
    BackoffSchedule sched;
    expect_within_jitter(sched.next_delay(), 1s);
}

TEST(AgentBackoffScheduleTest, DoublesUntilCap) {
    BackoffSchedule sched;
    // 수열(base): 1, 2, 4, 8, 16, 32->cap 30, 30, ...
    std::chrono::nanoseconds const bases[] = {1s, 2s, 4s, 8s, 16s, 30s, 30s};
    for (auto base : bases) {
        expect_within_jitter(sched.next_delay(), base);
    }
}

TEST(AgentBackoffScheduleTest, ResetRestartsAtBase) {
    BackoffSchedule sched;
    (void)sched.next_delay();
    (void)sched.next_delay();
    (void)sched.next_delay();
    EXPECT_EQ(sched.attempt(), 3u);

    sched.reset();
    EXPECT_EQ(sched.attempt(), 0u);
    expect_within_jitter(sched.next_delay(), 1s);
}

} // namespace
