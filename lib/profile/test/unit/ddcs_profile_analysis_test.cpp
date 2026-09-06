#include "ddcs/profile/analysis.hpp"

#include "ddcs/profile/recorder.hpp"
#include "ddcs/profile/serializer.hpp"

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

using ddcs::profile::AnalysisError;
using ddcs::profile::AnalysisWindow;
using ddcs::profile::Recorder;
using ddcs::profile::RunMetadata;
using ddcs::profile::TickOutcome;
using ddcs::profile::TickSample;

TickSample completed_sample(
    std::uint64_t tick_id, std::uint64_t started_ns, std::uint64_t command_ended_ns,
    std::uint64_t session_ended_ns, std::uint64_t finished_ns
) {
    return {
        .tick_id = tick_id,
        .started_ns = started_ns,
        .command_sweep_ended_ns = command_ended_ns,
        .session_sweep_ended_ns = session_ended_ns,
        .policy_evaluate_ended_ns = finished_ns,
        .finished_ns = finished_ns,
        .outcome = TickOutcome::completed,
    };
}

TickSample session_failure_sample() {
    return {
        .tick_id = 3,
        .started_ns = 200,
        .command_sweep_ended_ns = 210,
        .session_sweep_ended_ns = 0,
        .policy_evaluate_ended_ns = 0,
        .finished_ns = 220,
        .outcome = TickOutcome::session_sweep_threw,
    };
}

std::string serialize(Recorder& recorder) {
    auto const text = ddcs::profile::serialize_recording(
        recorder.finish(), RunMetadata{.run_id = "analysis-test"}
    );
    EXPECT_TRUE(text.has_value());
    return text.value_or("");
}

TEST(AnalysisTest, ComputesCompletedTickAndPhaseDistributionsSeparatelyFromFailures) {
    Recorder recorder{4};
    recorder.record(completed_sample(1, 0, 10, 30, 60));
    recorder.record(completed_sample(2, 100, 120, 130, 170));
    recorder.record(session_failure_sample());

    auto const result = ddcs::profile::analyze_recording(serialize(recorder));

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.summary.run_id, "analysis-test");
    EXPECT_TRUE(result.summary.recording_complete);
    EXPECT_EQ(result.summary.selected_ticks, 3u);
    EXPECT_EQ(result.summary.completed_ticks, 2u);
    EXPECT_EQ(result.summary.command_sweep_failures, 0u);
    EXPECT_EQ(result.summary.session_sweep_failures, 1u);
    EXPECT_EQ(result.summary.policy_evaluate_failures, 0u);

    EXPECT_EQ(result.summary.tick_duration.count, 2u);
    EXPECT_EQ(result.summary.tick_duration.mean_ns, 65u);
    EXPECT_EQ(result.summary.tick_duration.p95_ns, 70u);
    EXPECT_EQ(result.summary.tick_duration.max_ns, 70u);
    EXPECT_EQ(result.summary.command_sweep_duration.mean_ns, 15u);
    EXPECT_EQ(result.summary.session_sweep_duration.mean_ns, 15u);
    EXPECT_EQ(result.summary.policy_evaluate_duration.mean_ns, 35u);
    EXPECT_EQ(result.summary.start_interval.count, 2u);
    EXPECT_EQ(result.summary.start_interval.mean_ns, 100u);
    EXPECT_EQ(result.summary.start_interval_gaps, 0u);
}

TEST(AnalysisTest, SelectsTicksByTheirStartTimeAndDoesNotCrossWindowBoundariesForIntervals) {
    Recorder recorder{4};
    recorder.record(completed_sample(1, 0, 10, 30, 60));
    recorder.record(completed_sample(2, 100, 120, 130, 170));
    recorder.record(session_failure_sample());

    auto const result = ddcs::profile::analyze_recording(
        serialize(recorder), AnalysisWindow{.from_ns = 50, .to_ns = 200}
    );

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.summary.selected_ticks, 1u);
    EXPECT_EQ(result.summary.completed_ticks, 1u);
    EXPECT_EQ(result.summary.tick_duration.mean_ns, 70u);
    EXPECT_EQ(result.summary.start_interval.count, 0u);
}

TEST(AnalysisTest, MarksARecordingWithDroppedSamplesAsIncomplete) {
    Recorder recorder{1};
    recorder.record(completed_sample(1, 0, 10, 30, 60));
    recorder.record(completed_sample(2, 100, 120, 130, 170));

    auto const result = ddcs::profile::analyze_recording(serialize(recorder));

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.summary.captured, 1u);
    EXPECT_EQ(result.summary.dropped, 1u);
    EXPECT_FALSE(result.summary.recording_complete);
}

TEST(AnalysisTest, SelectsAnInclusiveTickPrefixAndFloorsEachTickBeforeSummingMicroseconds) {
    Recorder recorder{3};
    recorder.record(completed_sample(1, 0, 400, 1'000, 1'500));
    recorder.record(completed_sample(2, 2'000, 2'400, 3'000, 3'500));
    recorder.record(completed_sample(3, 4'000, 4'400, 5'000, 6'500));

    auto const result = ddcs::profile::analyze_recording_through_tick(serialize(recorder), 2);

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.summary.selected_ticks, 2u);
    EXPECT_EQ(result.summary.completed_ticks, 2u);
    EXPECT_EQ(result.summary.tick_duration.mean_ns, 1'500u);
    // 1,500 ns + 1,500 ns는 마지막에 한 번만 내리면 3 us지만, exporter와 같은 규약은 1 us
    // + 1 us다.
    EXPECT_EQ(result.summary.completed_tick_duration_us_total, 2u);
}

TEST(AnalysisTest, CountsEachExceptionalTickOutcomeWithoutAddingItToCompletedDuration) {
    Recorder recorder{3};
    recorder.record(
        TickSample{
            .tick_id = 1,
            .started_ns = 0,
            .command_sweep_ended_ns = 0,
            .session_sweep_ended_ns = 0,
            .policy_evaluate_ended_ns = 0,
            .finished_ns = 5,
            .outcome = TickOutcome::command_sweep_threw,
        }
    );
    recorder.record(
        TickSample{
            .tick_id = 2,
            .started_ns = 10,
            .command_sweep_ended_ns = 15,
            .session_sweep_ended_ns = 0,
            .policy_evaluate_ended_ns = 0,
            .finished_ns = 20,
            .outcome = TickOutcome::session_sweep_threw,
        }
    );
    recorder.record(
        TickSample{
            .tick_id = 3,
            .started_ns = 30,
            .command_sweep_ended_ns = 35,
            .session_sweep_ended_ns = 40,
            .policy_evaluate_ended_ns = 0,
            .finished_ns = 45,
            .outcome = TickOutcome::policy_evaluate_threw,
        }
    );

    auto const result = ddcs::profile::analyze_recording(serialize(recorder));

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.summary.selected_ticks, 3u);
    EXPECT_EQ(result.summary.completed_ticks, 0u);
    EXPECT_EQ(result.summary.completed_tick_duration_us_total, 0u);
    EXPECT_EQ(result.summary.command_sweep_failures, 1u);
    EXPECT_EQ(result.summary.session_sweep_failures, 1u);
    EXPECT_EQ(result.summary.policy_evaluate_failures, 1u);
    EXPECT_EQ(result.summary.tick_duration.count, 0u);
}

TEST(AnalysisTest, CountsTickIdGapsInsteadOfTreatingThemAsStartIntervals) {
    Recorder recorder{2};
    recorder.record(completed_sample(1, 0, 10, 30, 60));
    recorder.record(completed_sample(3, 200, 220, 230, 270));

    auto const result = ddcs::profile::analyze_recording(serialize(recorder));

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.summary.start_interval.count, 0u);
    EXPECT_EQ(result.summary.start_interval_gaps, 1u);
}

TEST(AnalysisTest, PreservesUnsignedTimestampsBeyondInt64) {
    constexpr std::uint64_t largest = std::numeric_limits<std::uint64_t>::max();
    Recorder recorder{1};
    recorder.record(completed_sample(largest, largest, largest, largest, largest));

    auto const result = ddcs::profile::analyze_recording(serialize(recorder));

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.summary.selected_ticks, 1u);
    EXPECT_EQ(result.summary.completed_ticks, 1u);
    EXPECT_EQ(result.summary.tick_duration.mean_ns, 0u);
}

TEST(AnalysisTest, ReadsAndValidatesTheOptionalUtcOriginBracket) {
    Recorder recorder{1};
    recorder.record(completed_sample(1, 0, 10, 30, 60));
    auto const text = ddcs::profile::serialize_recording(
        recorder.finish(), RunMetadata{
                               .run_id = "utc-analysis-test",
                               .recording_origin_utc = ddcs::profile::UtcClockBracket{
                                   .before_unix_ns = 1'700'000'000'000'000'000ULL,
                                   .after_unix_ns = 1'700'000'000'000'000'123ULL,
                               },
                           }
    );
    ASSERT_TRUE(text.has_value());

    auto const result = ddcs::profile::analyze_recording(*text);
    ASSERT_TRUE(result.succeeded());
    ASSERT_TRUE(result.summary.recording_origin_utc.has_value());
    EXPECT_EQ(result.summary.recording_origin_utc->before_unix_ns, 1'700'000'000'000'000'000ULL);
    EXPECT_EQ(result.summary.recording_origin_utc->after_unix_ns, 1'700'000'000'000'000'123ULL);

    auto invalid = *text;
    auto const first = invalid.find("1700000000000000000");
    ASSERT_NE(first, std::string::npos);
    invalid.replace(first, 19, "1700000000000000124");
    auto const invalid_result = ddcs::profile::analyze_recording(invalid);
    EXPECT_FALSE(invalid_result.succeeded());
    EXPECT_EQ(invalid_result.error, AnalysisError::invalid_recording);
}

TEST(AnalysisTest, RejectsUnsupportedSchemaAndInvalidWindows) {
    auto const unsupported = ddcs::profile::analyze_recording("{}");
    EXPECT_FALSE(unsupported.succeeded());
    EXPECT_EQ(unsupported.error, AnalysisError::unsupported_schema);

    auto const invalid_window =
        ddcs::profile::analyze_recording("{}", AnalysisWindow{.from_ns = 2, .to_ns = 1});
    EXPECT_FALSE(invalid_window.succeeded());
    EXPECT_EQ(invalid_window.error, AnalysisError::invalid_window);
}

} // namespace
