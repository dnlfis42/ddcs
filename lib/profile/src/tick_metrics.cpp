#include "ddcs/profile/tick_metrics.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace ddcs::profile {

namespace {

constexpr std::string_view ticks_total_name = "ddcs_ticks_total";
constexpr std::string_view tick_duration_total_name = "ddcs_tick_duration_seconds_total";
constexpr std::uint64_t microseconds_per_second = 1'000'000;

[[nodiscard]] TickMetricsParseResult failure(TickMetricsParseError error) noexcept {
    TickMetricsParseResult result;
    result.error = error;
    return result;
}

[[nodiscard]] std::string_view trim_left(std::string_view value) noexcept {
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t' || value.front() == '\r')) {
        value.remove_prefix(1);
    }
    return value;
}

[[nodiscard]] std::string_view trim_right(std::string_view value) noexcept {
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] std::optional<std::uint64_t> parse_uint64(std::string_view value) noexcept {
    if (value.empty()) {
        return std::nullopt;
    }

    std::uint64_t parsed = 0;
    auto const result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] std::optional<std::uint64_t>
parse_seconds_as_microseconds(std::string_view value) noexcept {
    auto const dot = value.find('.');
    auto const whole_text = value.substr(0, dot);
    auto const whole = parse_uint64(whole_text);
    if (!whole) {
        return std::nullopt;
    }

    std::uint64_t fractional = 0;
    if (dot != std::string_view::npos) {
        auto const fractional_text = value.substr(dot + 1);
        if (fractional_text.empty() || fractional_text.size() > 6) {
            return std::nullopt;
        }
        auto const parsed_fractional = parse_uint64(fractional_text);
        if (!parsed_fractional) {
            return std::nullopt;
        }
        fractional = *parsed_fractional;
        for (std::size_t index = fractional_text.size(); index < 6; ++index) {
            fractional *= 10;
        }
    }

    if (*whole >
        (std::numeric_limits<std::uint64_t>::max() - fractional) / microseconds_per_second) {
        return std::nullopt;
    }
    return *whole * microseconds_per_second + fractional;
}

[[nodiscard]] bool
looks_like_labeled_series(std::string_view token, std::string_view metric_name) noexcept {
    return token.size() > metric_name.size() && token.starts_with(metric_name) &&
           token[metric_name.size()] == '{';
}

[[nodiscard]] std::string_view next_line(std::string_view& input) noexcept {
    auto const newline = input.find('\n');
    if (newline == std::string_view::npos) {
        auto const line = input;
        input = {};
        return line;
    }

    auto const line = input.substr(0, newline);
    input.remove_prefix(newline + 1);
    return line;
}

template <typename Parser>
[[nodiscard]] bool read_metric_line(
    std::string_view line, std::string_view metric_name, std::optional<std::uint64_t>& destination,
    TickMetricsParseError duplicate_error, TickMetricsParseError invalid_error, Parser&& parser,
    TickMetricsParseError& error
) {
    line = trim_left(line);
    if (line.empty() || line.front() == '#') {
        return true;
    }

    auto const token_end = line.find_first_of(" \t\r");
    auto const token = line.substr(0, token_end);
    if (looks_like_labeled_series(token, metric_name)) {
        error = invalid_error;
        return false;
    }
    if (token != metric_name) {
        return true;
    }

    auto const value = trim_right(trim_left(line.substr(token.size())));
    if (value.empty() || value.find_first_of(" \t\r") != std::string_view::npos) {
        error = invalid_error;
        return false;
    }
    auto const parsed = parser(value);
    if (!parsed) {
        error = invalid_error;
        return false;
    }
    if (destination) {
        error = duplicate_error;
        return false;
    }
    destination = *parsed;
    return true;
}

} // namespace

TickMetricsParseResult parse_tick_metrics_snapshot(std::string_view prometheus_text) {
    std::optional<std::uint64_t> ticks_total;
    std::optional<std::uint64_t> tick_duration_us_total;
    TickMetricsParseError error = TickMetricsParseError::none;

    while (!prometheus_text.empty()) {
        auto const line = next_line(prometheus_text);
        if (!read_metric_line(
                line, ticks_total_name, ticks_total, TickMetricsParseError::duplicate_ticks_total,
                TickMetricsParseError::invalid_ticks_total, parse_uint64, error
            ) ||
            !read_metric_line(
                line, tick_duration_total_name, tick_duration_us_total,
                TickMetricsParseError::duplicate_tick_duration_total,
                TickMetricsParseError::invalid_tick_duration_total, parse_seconds_as_microseconds,
                error
            )) {
            return failure(error);
        }
    }

    if (!ticks_total) {
        return failure(TickMetricsParseError::missing_ticks_total);
    }
    if (!tick_duration_us_total) {
        return failure(TickMetricsParseError::missing_tick_duration_total);
    }
    return {
        .error = TickMetricsParseError::none,
        .snapshot = TickMetricsSnapshot{
            .ticks_total = *ticks_total,
            .tick_duration_us_total = *tick_duration_us_total,
        },
    };
}

TickMetricsCrosscheckResult verify_tick_metrics_snapshot(
    ProfileSummary const& raw_prefix, TickMetricsSnapshot metrics
) noexcept {
    TickMetricsCrosscheckResult result{
        .metrics = metrics,
        .raw_prefix_samples = raw_prefix.selected_ticks,
        .raw_completed_ticks = raw_prefix.completed_ticks,
        .raw_completed_tick_duration_us_total = raw_prefix.completed_tick_duration_us_total,
    };
    if (!raw_prefix.recording_complete || raw_prefix.dropped != 0) {
        result.error = TickMetricsCrosscheckError::incomplete_recording;
    } else if (raw_prefix.selected_ticks != metrics.ticks_total) {
        result.error = TickMetricsCrosscheckError::raw_prefix_sample_count_mismatch;
    } else if (raw_prefix.completed_ticks != metrics.ticks_total) {
        result.error = TickMetricsCrosscheckError::raw_prefix_completed_count_mismatch;
    } else if (raw_prefix.completed_tick_duration_us_total != metrics.tick_duration_us_total) {
        result.error = TickMetricsCrosscheckError::raw_prefix_duration_mismatch;
    }
    return result;
}

} // namespace ddcs::profile
