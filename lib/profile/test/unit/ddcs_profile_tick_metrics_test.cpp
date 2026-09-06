#include "ddcs/profile/tick_metrics.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::profile::ProfileSummary;
using ddcs::profile::TickMetricsCrosscheckError;
using ddcs::profile::TickMetricsParseError;
using ddcs::profile::TickMetricsSnapshot;

TEST(TickMetricsTest, ParsesTheTwoUnlabeledTickCountersWithExactMicrosecondRecovery) {
    auto const result = ddcs::profile::parse_tick_metrics_snapshot(
        "# HELP ddcs_ticks_total Completed Controller ticks.\n"
        "# TYPE ddcs_ticks_total counter\n"
        "ddcs_ticks_total 42\n"
        "ddcs_tick_duration_seconds_total 12.340005\n"
        "ddcs_other_metric{group=\"A B\"} 7\n"
    );

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.snapshot.ticks_total, 42u);
    EXPECT_EQ(result.snapshot.tick_duration_us_total, 12'340'005u);
}

TEST(TickMetricsTest, RejectsMissingDuplicateLabeledAndImpreciseRequiredCounters) {
    auto const missing = ddcs::profile::parse_tick_metrics_snapshot("ddcs_ticks_total 1\n");
    EXPECT_EQ(missing.error, TickMetricsParseError::missing_tick_duration_total);

    auto const duplicate = ddcs::profile::parse_tick_metrics_snapshot(
        "ddcs_ticks_total 1\n"
        "ddcs_ticks_total 2\n"
        "ddcs_tick_duration_seconds_total 0\n"
    );
    EXPECT_EQ(duplicate.error, TickMetricsParseError::duplicate_ticks_total);

    auto const labeled = ddcs::profile::parse_tick_metrics_snapshot(
        "ddcs_ticks_total{source=\"unexpected\"} 1\n"
        "ddcs_tick_duration_seconds_total 0\n"
    );
    EXPECT_EQ(labeled.error, TickMetricsParseError::invalid_ticks_total);

    auto const imprecise = ddcs::profile::parse_tick_metrics_snapshot(
        "ddcs_ticks_total 1\n"
        "ddcs_tick_duration_seconds_total 0.0000001\n"
    );
    EXPECT_EQ(imprecise.error, TickMetricsParseError::invalid_tick_duration_total);
}

TEST(TickMetricsTest, RequiresCompleteOneSamplePerTickPrefixAndExactDuration) {
    TickMetricsSnapshot const metrics{
        .ticks_total = 2,
        .tick_duration_us_total = 7,
    };
    ProfileSummary complete;
    complete.recording_complete = true;
    complete.selected_ticks = 2;
    complete.completed_ticks = 2;
    complete.completed_tick_duration_us_total = 7;

    auto const verified = ddcs::profile::verify_tick_metrics_snapshot(complete, metrics);
    EXPECT_TRUE(verified.succeeded());
    EXPECT_EQ(verified.raw_prefix_samples, 2u);
    EXPECT_EQ(verified.raw_completed_ticks, 2u);
    EXPECT_EQ(verified.raw_completed_tick_duration_us_total, 7u);

    auto dropped = complete;
    dropped.dropped = 1;
    dropped.recording_complete = false;
    EXPECT_EQ(
        ddcs::profile::verify_tick_metrics_snapshot(dropped, metrics).error,
        TickMetricsCrosscheckError::incomplete_recording
    );

    auto missing_sample = complete;
    missing_sample.selected_ticks = 1;
    EXPECT_EQ(
        ddcs::profile::verify_tick_metrics_snapshot(missing_sample, metrics).error,
        TickMetricsCrosscheckError::raw_prefix_sample_count_mismatch
    );

    auto incomplete_tick = complete;
    incomplete_tick.completed_ticks = 1;
    EXPECT_EQ(
        ddcs::profile::verify_tick_metrics_snapshot(incomplete_tick, metrics).error,
        TickMetricsCrosscheckError::raw_prefix_completed_count_mismatch
    );

    auto different_duration = complete;
    different_duration.completed_tick_duration_us_total = 8;
    EXPECT_EQ(
        ddcs::profile::verify_tick_metrics_snapshot(different_duration, metrics).error,
        TickMetricsCrosscheckError::raw_prefix_duration_mismatch
    );
}

} // namespace
