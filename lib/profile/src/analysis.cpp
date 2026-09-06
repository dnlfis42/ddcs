#include "ddcs/profile/analysis.hpp"

#include "ddcs/json/value.hpp"
#include "ddcs/profile/tick_sample.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ddcs::profile {

namespace {

struct DistributionAccumulator {
    [[nodiscard]] bool add(std::uint64_t value) {
        if (sum > std::numeric_limits<std::uint64_t>::max() - value) {
            return false;
        }

        sum += value;
        values.push_back(value);
        return true;
    }

    [[nodiscard]] Distribution finish() {
        Distribution result;
        result.count = static_cast<std::uint64_t>(values.size());
        if (values.empty()) {
            return result;
        }

        std::sort(values.begin(), values.end());
        // nearest-rank p95 = ceil(19n / 20). n - floor(n / 20)은 overflow 없이 같은 값이다.
        auto const rank = values.size() - values.size() / 20;
        result.mean_ns = sum / result.count;
        result.p95_ns = values[rank - 1];
        result.max_ns = values.back();
        return result;
    }

    std::vector<std::uint64_t> values;
    std::uint64_t sum = 0;
};

struct Accumulators {
    DistributionAccumulator tick_duration;
    DistributionAccumulator command_sweep_duration;
    DistributionAccumulator session_sweep_duration;
    DistributionAccumulator policy_evaluate_duration;
    DistributionAccumulator start_interval;
};

[[nodiscard]] AnalysisResult failure(AnalysisError error) {
    AnalysisResult result;
    result.error = error;
    return result;
}

[[nodiscard]] bool read_uint_member(
    ddcs::json::Value const& object, std::string_view key, std::uint64_t& value
) noexcept {
    auto const* member = object.find(key);
    if (member == nullptr) {
        return false;
    }

    auto const number = member->as_uint64();
    if (!number) {
        return false;
    }

    value = *number;
    return true;
}

[[nodiscard]] bool read_string_member(
    ddcs::json::Value const& object, std::string_view key, std::string_view& value
) noexcept {
    auto const* member = object.find(key);
    if (member == nullptr) {
        return false;
    }

    auto const text = member->as_string();
    if (!text) {
        return false;
    }

    value = *text;
    return true;
}

[[nodiscard]] bool read_recording_origin_utc(
    ddcs::json::Value const& root, std::optional<UtcClockBracket>& bracket
) noexcept {
    auto const* origin = root.find("recording_origin_utc");
    // schema v1의 초기 dump에는 이 확장 metadata가 없었다. 분석은 기존 raw 파일도 읽는다.
    if (origin == nullptr || origin->is_null()) {
        return true;
    }
    if (!origin->is_object()) {
        return false;
    }

    UtcClockBracket parsed{};
    if (!read_uint_member(*origin, "before_unix_ns", parsed.before_unix_ns) ||
        !read_uint_member(*origin, "after_unix_ns", parsed.after_unix_ns) ||
        parsed.before_unix_ns > parsed.after_unix_ns) {
        return false;
    }

    bracket = parsed;
    return true;
}

[[nodiscard]] std::optional<TickOutcome> parse_outcome(std::string_view text) noexcept {
    if (text == "completed") {
        return TickOutcome::completed;
    }
    if (text == "command_sweep_threw") {
        return TickOutcome::command_sweep_threw;
    }
    if (text == "session_sweep_threw") {
        return TickOutcome::session_sweep_threw;
    }
    if (text == "policy_evaluate_threw") {
        return TickOutcome::policy_evaluate_threw;
    }

    return std::nullopt;
}

[[nodiscard]] bool read_endpoint(
    ddcs::json::Value const& object, std::string_view key, bool completed, std::uint64_t& value
) noexcept {
    auto const* member = object.find(key);
    if (member == nullptr) {
        return false;
    }

    if (!completed) {
        return member->is_null();
    }

    auto const number = member->as_uint64();
    if (!number) {
        return false;
    }

    value = *number;
    return true;
}

[[nodiscard]] std::optional<TickSample> parse_sample(ddcs::json::Value const& value) noexcept {
    if (!value.is_object()) {
        return std::nullopt;
    }

    TickSample sample{};
    std::string_view outcome_text;
    if (!read_uint_member(value, "tick_id", sample.tick_id) ||
        !read_uint_member(value, "started_ns", sample.started_ns) ||
        !read_uint_member(value, "finished_ns", sample.finished_ns) ||
        !read_string_member(value, "outcome", outcome_text)) {
        return std::nullopt;
    }

    auto const outcome = parse_outcome(outcome_text);
    if (!outcome) {
        return std::nullopt;
    }
    sample.outcome = *outcome;

    if (!read_endpoint(
            value, "command_sweep_ended_ns", command_sweep_completed(sample.outcome),
            sample.command_sweep_ended_ns
        ) ||
        !read_endpoint(
            value, "session_sweep_ended_ns", session_sweep_completed(sample.outcome),
            sample.session_sweep_ended_ns
        ) ||
        !read_endpoint(
            value, "policy_evaluate_ended_ns", policy_evaluate_completed(sample.outcome),
            sample.policy_evaluate_ended_ns
        ) ||
        !is_valid_tick_sample(sample)) {
        return std::nullopt;
    }

    return sample;
}

[[nodiscard]] bool in_window(
    TickSample const& sample, AnalysisWindow const& window,
    std::optional<std::uint64_t> through_tick_id
) noexcept {
    if (window.from_ns && sample.started_ns < *window.from_ns) {
        return false;
    }
    if (window.to_ns && sample.started_ns >= *window.to_ns) {
        return false;
    }
    if (through_tick_id && sample.tick_id > *through_tick_id) {
        return false;
    }

    return true;
}

[[nodiscard]] bool increment(std::uint64_t& value) noexcept {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }

    ++value;
    return true;
}

[[nodiscard]] bool add(std::uint64_t& value, std::uint64_t addend) noexcept {
    if (value > std::numeric_limits<std::uint64_t>::max() - addend) {
        return false;
    }

    value += addend;
    return true;
}

[[nodiscard]] bool add_completed_durations(
    TickSample const& sample, ProfileSummary& summary, Accumulators& accumulators
) {
    auto const tick_duration_ns = sample.finished_ns - sample.started_ns;
    auto const tick_duration_us = tick_duration_ns / 1'000;
    if (!accumulators.tick_duration.add(tick_duration_ns) ||
        !accumulators.command_sweep_duration.add(
            sample.command_sweep_ended_ns - sample.started_ns
        ) ||
        !accumulators.session_sweep_duration.add(
            sample.session_sweep_ended_ns - sample.command_sweep_ended_ns
        ) ||
        !accumulators.policy_evaluate_duration.add(
            sample.policy_evaluate_ended_ns - sample.session_sweep_ended_ns
        ) ||
        !add(summary.completed_tick_duration_us_total, tick_duration_us)) {
        return false;
    }

    return increment(summary.completed_ticks);
}

[[nodiscard]] AnalysisResult analyze_recording_impl(
    std::string_view json_text, AnalysisWindow const& window,
    std::optional<std::uint64_t> through_tick_id
) {
    if (window.from_ns && window.to_ns && *window.from_ns > *window.to_ns) {
        return failure(AnalysisError::invalid_window);
    }

    auto const root = ddcs::json::parse(json_text);
    if (!root) {
        return failure(AnalysisError::invalid_json);
    }
    if (!root->is_object()) {
        return failure(AnalysisError::invalid_recording);
    }

    std::string_view schema_name;
    std::string_view time_unit;
    std::uint64_t schema_version = 0;
    if (!read_string_member(*root, "schema_name", schema_name) ||
        !read_uint_member(*root, "schema_version", schema_version) ||
        !read_string_member(*root, "time_unit", time_unit) ||
        schema_name != tick_profile_schema_name || schema_version != tick_profile_schema_version ||
        time_unit != tick_profile_time_unit) {
        return failure(AnalysisError::unsupported_schema);
    }

    std::string_view run_id;
    auto const* recording = root->find("recording");
    auto const* samples = root->find("samples");
    if (!read_string_member(*root, "run_id", run_id) || run_id.empty() || recording == nullptr ||
        !recording->is_object() || samples == nullptr || !samples->is_array()) {
        return failure(AnalysisError::invalid_recording);
    }

    ProfileSummary summary;
    summary.run_id = run_id;
    std::uint64_t storage_bytes = 0;
    if (!read_uint_member(*recording, "capacity", summary.capacity) ||
        !read_uint_member(*recording, "storage_bytes", storage_bytes) ||
        !read_uint_member(*recording, "captured", summary.captured) ||
        !read_uint_member(*recording, "dropped", summary.dropped) || summary.capacity == 0 ||
        storage_bytes == 0 || summary.captured > summary.capacity ||
        summary.captured != static_cast<std::uint64_t>(samples->size())) {
        return failure(AnalysisError::invalid_recording);
    }
    if (!read_recording_origin_utc(*root, summary.recording_origin_utc)) {
        return failure(AnalysisError::invalid_recording);
    }
    summary.recording_complete = summary.dropped == 0;

    Accumulators accumulators;
    std::optional<TickSample> previous;
    bool previous_selected = false;

    for (std::size_t index = 0; index < samples->size(); ++index) {
        auto const* sample_value = samples->at(index);
        if (sample_value == nullptr) {
            return failure(AnalysisError::invalid_recording);
        }

        auto const sample = parse_sample(*sample_value);
        if (!sample) {
            return failure(AnalysisError::invalid_recording);
        }
        if (previous &&
            (sample->tick_id <= previous->tick_id || sample->started_ns < previous->started_ns)) {
            return failure(AnalysisError::invalid_recording);
        }

        bool const selected = in_window(*sample, window, through_tick_id);
        if (selected) {
            if (!increment(summary.selected_ticks)) {
                return failure(AnalysisError::arithmetic_overflow);
            }

            if (previous && previous_selected) {
                if (sample->tick_id - previous->tick_id == 1) {
                    if (!accumulators.start_interval.add(
                            sample->started_ns - previous->started_ns
                        )) {
                        return failure(AnalysisError::arithmetic_overflow);
                    }
                } else if (!increment(summary.start_interval_gaps)) {
                    return failure(AnalysisError::arithmetic_overflow);
                }
            }

            switch (sample->outcome) {
            case TickOutcome::completed:
                if (!add_completed_durations(*sample, summary, accumulators)) {
                    return failure(AnalysisError::arithmetic_overflow);
                }
                break;
            case TickOutcome::command_sweep_threw:
                if (!increment(summary.command_sweep_failures)) {
                    return failure(AnalysisError::arithmetic_overflow);
                }
                break;
            case TickOutcome::session_sweep_threw:
                if (!increment(summary.session_sweep_failures)) {
                    return failure(AnalysisError::arithmetic_overflow);
                }
                break;
            case TickOutcome::policy_evaluate_threw:
                if (!increment(summary.policy_evaluate_failures)) {
                    return failure(AnalysisError::arithmetic_overflow);
                }
                break;
            }
        }

        previous = *sample;
        previous_selected = selected;
    }

    summary.tick_duration = accumulators.tick_duration.finish();
    summary.command_sweep_duration = accumulators.command_sweep_duration.finish();
    summary.session_sweep_duration = accumulators.session_sweep_duration.finish();
    summary.policy_evaluate_duration = accumulators.policy_evaluate_duration.finish();
    summary.start_interval = accumulators.start_interval.finish();

    AnalysisResult result;
    result.summary = std::move(summary);
    return result;
}

} // namespace

AnalysisResult analyze_recording(std::string_view json_text, AnalysisWindow const& window) {
    return analyze_recording_impl(json_text, window, std::nullopt);
}

AnalysisResult
analyze_recording_through_tick(std::string_view json_text, std::uint64_t through_tick_id) {
    return analyze_recording_impl(json_text, {}, through_tick_id);
}

} // namespace ddcs::profile
