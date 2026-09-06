#include "ddcs/json/value.hpp"
#include "ddcs/profile/recorder.hpp"
#include "ddcs/profile/serializer.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::profile::Recorder;
using ddcs::profile::RunMetadata;
using ddcs::profile::TickOutcome;
using ddcs::profile::TickSample;

TickSample completed_sample() {
    return {
        .tick_id = 1,
        .started_ns = 10,
        .command_sweep_ended_ns = 13,
        .session_sweep_ended_ns = 15,
        .policy_evaluate_ended_ns = 19,
        .finished_ns = 19,
        .outcome = TickOutcome::completed,
    };
}

TickSample session_failure_sample() {
    return {
        .tick_id = 2,
        .started_ns = 20,
        .command_sweep_ended_ns = 23,
        .session_sweep_ended_ns = 0,
        .policy_evaluate_ended_ns = 0,
        .finished_ns = 27,
        .outcome = TickOutcome::session_sweep_threw,
    };
}

RunMetadata metadata() {
    return {.run_id = "serializer-test"};
}

TEST(SerializerTest, WritesSchemaMetadataAndRawSamples) {
    Recorder recorder{3};
    recorder.record(completed_sample());
    recorder.record(session_failure_sample());

    auto const serialized = ddcs::profile::serialize_recording(recorder.finish(), metadata());
    ASSERT_TRUE(serialized.has_value());

    auto const root = ddcs::json::parse(*serialized);
    ASSERT_TRUE(root.has_value());
    ASSERT_EQ(root->find("schema_name")->as_string(), "ddcs.tick_profile");
    ASSERT_EQ(root->find("schema_version")->as_int64(), 1);
    ASSERT_EQ(root->find("time_unit")->as_string(), "ns");
    ASSERT_EQ(root->find("run_id")->as_string(), "serializer-test");
    ASSERT_TRUE(root->find("recording_origin_utc")->is_null());

    auto const* recording = root->find("recording");
    ASSERT_NE(recording, nullptr);
    ASSERT_EQ(recording->find("capacity")->as_int64(), 3);
    ASSERT_EQ(recording->find("storage_bytes")->as_int64(), 3 * sizeof(TickSample));
    ASSERT_EQ(recording->find("captured")->as_int64(), 2);
    ASSERT_EQ(recording->find("dropped")->as_int64(), 0);

    auto const* samples = root->find("samples");
    ASSERT_NE(samples, nullptr);
    ASSERT_EQ(samples->size(), 2u);

    auto const* completed = samples->at(0);
    ASSERT_NE(completed, nullptr);
    ASSERT_EQ(completed->find("tick_id")->as_int64(), 1);
    ASSERT_EQ(completed->find("started_ns")->as_int64(), 10);
    ASSERT_EQ(completed->find("command_sweep_ended_ns")->as_int64(), 13);
    ASSERT_EQ(completed->find("session_sweep_ended_ns")->as_int64(), 15);
    ASSERT_EQ(completed->find("policy_evaluate_ended_ns")->as_int64(), 19);
    ASSERT_EQ(completed->find("finished_ns")->as_int64(), 19);
    ASSERT_EQ(completed->find("outcome")->as_string(), "completed");

    auto const* session_failure = samples->at(1);
    ASSERT_NE(session_failure, nullptr);
    ASSERT_EQ(session_failure->find("tick_id")->as_int64(), 2);
    ASSERT_EQ(session_failure->find("command_sweep_ended_ns")->as_int64(), 23);
    ASSERT_TRUE(session_failure->find("session_sweep_ended_ns")->is_null());
    ASSERT_TRUE(session_failure->find("policy_evaluate_ended_ns")->is_null());
    ASSERT_EQ(session_failure->find("finished_ns")->as_int64(), 27);
    ASSERT_EQ(session_failure->find("outcome")->as_string(), "session_sweep_threw");
}

TEST(SerializerTest, WritesTheUtcBracketForTheRecordingOrigin) {
    Recorder recorder{1};
    auto const serialized = ddcs::profile::serialize_recording(
        recorder.finish(), RunMetadata{
                               .run_id = "utc-bracket-test",
                               .recording_origin_utc = ddcs::profile::UtcClockBracket{
                                   .before_unix_ns = 1'700'000'000'000'000'000ULL,
                                   .after_unix_ns = 1'700'000'000'000'000'123ULL,
                               },
                           }
    );
    ASSERT_TRUE(serialized.has_value());

    auto const root = ddcs::json::parse(*serialized);
    ASSERT_TRUE(root.has_value());
    auto const* bracket = root->find("recording_origin_utc");
    ASSERT_NE(bracket, nullptr);
    ASSERT_EQ(bracket->find("before_unix_ns")->as_uint64(), 1'700'000'000'000'000'000ULL);
    ASSERT_EQ(bracket->find("after_unix_ns")->as_uint64(), 1'700'000'000'000'000'123ULL);
}

TEST(SerializerTest, PreservesAnEmptyRecording) {
    Recorder recorder{1};

    auto const serialized = ddcs::profile::serialize_recording(recorder.finish(), metadata());
    ASSERT_TRUE(serialized.has_value());

    auto const root = ddcs::json::parse(*serialized);
    ASSERT_TRUE(root.has_value());
    auto const* recording = root->find("recording");
    ASSERT_NE(recording, nullptr);
    ASSERT_EQ(recording->find("capacity")->as_int64(), 1);
    ASSERT_EQ(recording->find("captured")->as_int64(), 0);
    ASSERT_EQ(recording->find("dropped")->as_int64(), 0);

    auto const* samples = root->find("samples");
    ASSERT_NE(samples, nullptr);
    EXPECT_EQ(samples->size(), 0u);
}

TEST(SerializerTest, PreservesDropMetadata) {
    Recorder recorder{1};
    recorder.record(completed_sample());

    auto dropped = completed_sample();
    dropped.tick_id = 2;
    recorder.record(dropped);

    auto const serialized = ddcs::profile::serialize_recording(recorder.finish(), metadata());
    ASSERT_TRUE(serialized.has_value());

    auto const root = ddcs::json::parse(*serialized);
    ASSERT_TRUE(root.has_value());
    auto const* recording = root->find("recording");
    ASSERT_NE(recording, nullptr);
    ASSERT_EQ(recording->find("capacity")->as_int64(), 1);
    ASSERT_EQ(recording->find("captured")->as_int64(), 1);
    ASSERT_EQ(recording->find("dropped")->as_int64(), 1);
}

TEST(SerializerTest, RejectsAnInvalidRecordedSample) {
    Recorder recorder{1};
    auto invalid = completed_sample();
    invalid.tick_id = 0;
    recorder.record(invalid);

    EXPECT_FALSE(ddcs::profile::serialize_recording(recorder.finish(), metadata()).has_value());
}

TEST(SerializerTest, RejectsEmptyRunId) {
    Recorder recorder{1};

    EXPECT_FALSE(
        ddcs::profile::serialize_recording(recorder.finish(), RunMetadata{.run_id = ""}).has_value()
    );
}

TEST(SerializerTest, RejectsAReversedUtcBracket) {
    Recorder recorder{1};

    EXPECT_FALSE(
        ddcs::profile::serialize_recording(
            recorder.finish(),
            RunMetadata{
                .run_id = "reversed-utc-bracket",
                .recording_origin_utc =
                    ddcs::profile::UtcClockBracket{
                        .before_unix_ns = 2,
                        .after_unix_ns = 1,
                    },
            }
        )
            .has_value()
    );
}

} // namespace
