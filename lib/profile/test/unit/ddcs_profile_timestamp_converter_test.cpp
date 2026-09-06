#include "ddcs/common/clock.hpp"
#include "ddcs/profile/tick_sample.hpp"
#include "ddcs/profile/timestamp_converter.hpp"

#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::common::Clock;
using ddcs::common::ManualClock;
using ddcs::profile::TickOutcome;
using ddcs::profile::TickSample;
using ddcs::profile::TimestampConverter;

TEST(TimestampConverterTest, ConvertsManualClockTimesRelativeToTheOrigin) {
    ManualClock clock{Clock::time_point{std::chrono::nanoseconds{100}}};
    TimestampConverter const converter{clock.now()};

    EXPECT_EQ(converter.relative_ns(clock.now()), 0u);

    clock.advance(std::chrono::nanoseconds{37});

    ASSERT_TRUE(converter.relative_ns(clock.now()).has_value());
    EXPECT_EQ(*converter.relative_ns(clock.now()), 37u);
}

TEST(TimestampConverterTest, RejectsATimeBeforeTheOrigin) {
    TimestampConverter const converter{Clock::time_point{std::chrono::nanoseconds{100}}};

    EXPECT_FALSE(
        converter.relative_ns(Clock::time_point{std::chrono::nanoseconds{99}}).has_value()
    );
}

TickSample completed_sample() {
    return {
        .tick_id = 1,
        .started_ns = 0,
        .command_sweep_ended_ns = 3,
        .session_sweep_ended_ns = 5,
        .policy_evaluate_ended_ns = 8,
        .finished_ns = 8,
        .outcome = TickOutcome::completed,
    };
}

TEST(TickSampleTest, AcceptsValidCompletedAndExceptionSamples) {
    auto completed = completed_sample();
    EXPECT_TRUE(ddcs::profile::is_valid_tick_sample(completed));

    auto command_threw = completed;
    command_threw.command_sweep_ended_ns = 0;
    command_threw.session_sweep_ended_ns = 0;
    command_threw.policy_evaluate_ended_ns = 0;
    command_threw.finished_ns = 2;
    command_threw.outcome = TickOutcome::command_sweep_threw;
    EXPECT_TRUE(ddcs::profile::is_valid_tick_sample(command_threw));

    auto session_threw = completed;
    session_threw.session_sweep_ended_ns = 0;
    session_threw.policy_evaluate_ended_ns = 0;
    session_threw.finished_ns = 6;
    session_threw.outcome = TickOutcome::session_sweep_threw;
    EXPECT_TRUE(ddcs::profile::is_valid_tick_sample(session_threw));

    auto policy_threw = completed;
    policy_threw.policy_evaluate_ended_ns = 0;
    policy_threw.finished_ns = 9;
    policy_threw.outcome = TickOutcome::policy_evaluate_threw;
    EXPECT_TRUE(ddcs::profile::is_valid_tick_sample(policy_threw));
}

TEST(TickSampleTest, RejectsUnknownOutcomesAndInconsistentBoundaries) {
    auto sample = completed_sample();
    sample.finished_ns = 9;
    EXPECT_FALSE(ddcs::profile::is_valid_tick_sample(sample));

    sample = completed_sample();
    sample.tick_id = 0;
    EXPECT_FALSE(ddcs::profile::is_valid_tick_sample(sample));

    sample = completed_sample();
    sample.outcome = static_cast<TickOutcome>(99);
    EXPECT_FALSE(ddcs::profile::is_known_outcome(sample.outcome));
    EXPECT_FALSE(ddcs::profile::is_valid_tick_sample(sample));
}

} // namespace
