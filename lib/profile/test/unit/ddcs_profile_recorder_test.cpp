#include "ddcs/profile/recorder.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

using ddcs::profile::Recorder;
using ddcs::profile::TickOutcome;
using ddcs::profile::TickSample;

TickSample sample(std::uint64_t tick_id) {
    return {
        .tick_id = tick_id,
        .started_ns = tick_id * 10,
        .command_sweep_ended_ns = tick_id * 10 + 1,
        .session_sweep_ended_ns = tick_id * 10 + 2,
        .policy_evaluate_ended_ns = tick_id * 10 + 3,
        .finished_ns = tick_id * 10 + 3,
        .outcome = TickOutcome::completed,
    };
}

TEST(RecorderTest, AllocatesTheRequestedFixedCapacity) {
    Recorder recorder{2};

    EXPECT_EQ(recorder.capacity(), 2u);
    EXPECT_EQ(recorder.storage_bytes(), 2u * sizeof(TickSample));
    EXPECT_FALSE(recorder.finished());

    auto const view = recorder.finish();

    EXPECT_TRUE(view.samples().empty());
    EXPECT_EQ(view.capacity(), 2u);
    EXPECT_EQ(view.storage_bytes(), 2u * sizeof(TickSample));
    EXPECT_EQ(view.dropped(), 0u);
}

TEST(RecorderTest, RejectsZeroCapacity) {
    EXPECT_THROW(Recorder{0}, std::invalid_argument);
}

TEST(RecorderTest, RejectsStorageSizeOverflowBeforeAllocating) {
    auto const capacity = std::numeric_limits<std::size_t>::max() / sizeof(TickSample) + 1;

    EXPECT_THROW(Recorder{capacity}, std::length_error);
}

TEST(RecorderTest, CapturesSamplesInRecordOrderAndDropsOnlyNewOverflow) {
    Recorder recorder{2};

    recorder.record(sample(1));
    recorder.record(sample(2));
    recorder.record(sample(3));

    auto const view = recorder.finish();

    ASSERT_EQ(view.samples().size(), 2u);
    EXPECT_EQ(view.samples()[0].tick_id, 1u);
    EXPECT_EQ(view.samples()[1].tick_id, 2u);
    EXPECT_EQ(view.dropped(), 1u);
}

TEST(RecorderTest, RepeatedFinishAndLaterRecordDoNotChangeTheRecording) {
    Recorder recorder{2};
    recorder.record(sample(1));

    auto const first = recorder.finish();
    recorder.record(sample(2));
    auto const second = recorder.finish();

    ASSERT_EQ(first.samples().size(), 1u);
    EXPECT_EQ(first.samples()[0].tick_id, 1u);
    EXPECT_EQ(first.dropped(), 0u);

    ASSERT_EQ(second.samples().size(), 1u);
    EXPECT_EQ(second.samples()[0].tick_id, 1u);
    EXPECT_EQ(second.dropped(), 0u);
    EXPECT_TRUE(recorder.finished());
}

} // namespace
