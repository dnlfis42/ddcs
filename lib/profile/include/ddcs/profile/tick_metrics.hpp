#pragma once

#include "ddcs/profile/analysis.hpp"

#include <cstdint>
#include <string_view>

namespace ddcs::profile {

// Controller가 tick profile과 같은 작업 단위를 노출하는, label 없는 두 Prometheus counter의
// snapshot이다. duration은 exporter의 seconds 표기를 정확히 복원한 정수 microseconds다.
struct TickMetricsSnapshot {
    std::uint64_t ticks_total = 0;
    std::uint64_t tick_duration_us_total = 0;
};

enum class TickMetricsParseError {
    none,
    missing_ticks_total,
    missing_tick_duration_total,
    duplicate_ticks_total,
    duplicate_tick_duration_total,
    invalid_ticks_total,
    invalid_tick_duration_total,
};

[[nodiscard]] constexpr std::string_view to_string(TickMetricsParseError error) noexcept {
    switch (error) {
    case TickMetricsParseError::none:
        return "none";
    case TickMetricsParseError::missing_ticks_total:
        return "missing_ticks_total";
    case TickMetricsParseError::missing_tick_duration_total:
        return "missing_tick_duration_total";
    case TickMetricsParseError::duplicate_ticks_total:
        return "duplicate_ticks_total";
    case TickMetricsParseError::duplicate_tick_duration_total:
        return "duplicate_tick_duration_total";
    case TickMetricsParseError::invalid_ticks_total:
        return "invalid_ticks_total";
    case TickMetricsParseError::invalid_tick_duration_total:
        return "invalid_tick_duration_total";
    }

    return "unknown";
}

struct TickMetricsParseResult {
    TickMetricsParseError error = TickMetricsParseError::none;
    TickMetricsSnapshot snapshot;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == TickMetricsParseError::none;
    }
};

// DDCS exporter가 남긴 Prometheus text에서 tick counter 두 개를 엄격하게 읽는다. 두 series는
// label과 timestamp가 없어야 하며, seconds는 최대 6자리 fractional precision으로 복원한다.
[[nodiscard]] TickMetricsParseResult parse_tick_metrics_snapshot(std::string_view prometheus_text);

enum class TickMetricsCrosscheckError {
    none,
    incomplete_recording,
    raw_prefix_sample_count_mismatch,
    raw_prefix_completed_count_mismatch,
    raw_prefix_duration_mismatch,
};

[[nodiscard]] constexpr std::string_view to_string(TickMetricsCrosscheckError error) noexcept {
    switch (error) {
    case TickMetricsCrosscheckError::none:
        return "none";
    case TickMetricsCrosscheckError::incomplete_recording:
        return "incomplete_recording";
    case TickMetricsCrosscheckError::raw_prefix_sample_count_mismatch:
        return "raw_prefix_sample_count_mismatch";
    case TickMetricsCrosscheckError::raw_prefix_completed_count_mismatch:
        return "raw_prefix_completed_count_mismatch";
    case TickMetricsCrosscheckError::raw_prefix_duration_mismatch:
        return "raw_prefix_duration_mismatch";
    }

    return "unknown";
}

struct TickMetricsCrosscheckResult {
    TickMetricsCrosscheckError error = TickMetricsCrosscheckError::none;
    TickMetricsSnapshot metrics;
    std::uint64_t raw_prefix_samples = 0;
    std::uint64_t raw_completed_ticks = 0;
    std::uint64_t raw_completed_tick_duration_us_total = 0;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == TickMetricsCrosscheckError::none;
    }
};

// raw_prefix는 metric ticks_total을 analyze_recording_through_tick()에 넣어 얻은 결과여야 한다.
// dropped sample, prefix의 ID 누락/예외, 또는 per-tick microsecond 합계의 불일치를 모두 실패로
// 돌려 raw profile과 Prometheus counter를 같은 관측으로 묶는다.
[[nodiscard]] TickMetricsCrosscheckResult verify_tick_metrics_snapshot(
    ProfileSummary const& raw_prefix, TickMetricsSnapshot metrics
) noexcept;

} // namespace ddcs::profile
