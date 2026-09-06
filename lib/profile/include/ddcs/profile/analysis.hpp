#pragma once

#include "ddcs/profile/recording_metadata.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ddcs::profile {

struct AnalysisWindow {
    // tick 시작 시각 기준 [from_ns, to_ns). 비어 있으면 각각 하한/상한 없음.
    std::optional<std::uint64_t> from_ns;
    std::optional<std::uint64_t> to_ns;
};

struct Distribution {
    std::uint64_t count = 0;
    std::optional<std::uint64_t> mean_ns;
    std::optional<std::uint64_t> p95_ns;
    std::optional<std::uint64_t> max_ns;
};

struct ProfileSummary {
    std::string run_id;
    // raw JSON에 없거나 null이면 UTC 기반 측정 창으로 변환할 수 없다.
    std::optional<UtcClockBracket> recording_origin_utc;
    std::uint64_t capacity = 0;
    std::uint64_t captured = 0;
    std::uint64_t dropped = 0;
    bool recording_complete = false;

    std::uint64_t selected_ticks = 0;
    std::uint64_t completed_ticks = 0;
    // 완료 tick마다 ns duration을 microseconds로 내림한 뒤 합한 값. Controller의
    // ddcs_tick_duration_seconds_total과 같은 절삭 규약이다.
    std::uint64_t completed_tick_duration_us_total = 0;
    std::uint64_t command_sweep_failures = 0;
    std::uint64_t session_sweep_failures = 0;
    std::uint64_t policy_evaluate_failures = 0;

    Distribution tick_duration;
    Distribution command_sweep_duration;
    Distribution session_sweep_duration;
    Distribution policy_evaluate_duration;
    Distribution start_interval;
    std::uint64_t start_interval_gaps = 0;
};

enum class AnalysisError {
    none,
    invalid_json,
    unsupported_schema,
    invalid_window,
    invalid_recording,
    arithmetic_overflow,
};

[[nodiscard]] constexpr std::string_view to_string(AnalysisError error) noexcept {
    switch (error) {
    case AnalysisError::none:
        return "none";
    case AnalysisError::invalid_json:
        return "invalid_json";
    case AnalysisError::unsupported_schema:
        return "unsupported_schema";
    case AnalysisError::invalid_window:
        return "invalid_window";
    case AnalysisError::invalid_recording:
        return "invalid_recording";
    case AnalysisError::arithmetic_overflow:
        return "arithmetic_overflow";
    }

    return "unknown";
}

struct AnalysisResult {
    AnalysisError error = AnalysisError::none;
    ProfileSummary summary;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == AnalysisError::none;
    }
};

// schema_version 1 raw dump을 엄격히 읽어 완료 tick의 duration 분포와 시작 간격을 계산한다.
// p95는 nearest-rank(ceil(0.95 * count))이며, mean은 정수 ns의 내림이다.
[[nodiscard]] AnalysisResult
analyze_recording(std::string_view json_text, AnalysisWindow const& window = {});

// tick ID 기준 (-inf, through_tick_id] prefix를 분석한다. Prometheus snapshot 시점의 raw
// 표본을 고를 때 쓰며, 시간 창과는 섞지 않는다.
[[nodiscard]] AnalysisResult
analyze_recording_through_tick(std::string_view json_text, std::uint64_t through_tick_id);

} // namespace ddcs::profile
