#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include "ddcs/ctrl/app/device/group_aggregate.hpp"
#include "ddcs/device/mode.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace ddcs::ctrl::app::metrics {

namespace {

void append_metric_header(std::string& out, char const* name, char const* help, char const* type) {
    out += "# HELP ";
    out += name;
    out += ' ';
    out += help;
    out += "\n# TYPE ";
    out += name;
    out += ' ';
    out += type;
    out += '\n';
}

// 정수 microseconds를 locale/부동소수 오차 없이 Prometheus base unit seconds로 쓴다.
void append_microseconds_as_seconds(std::string& out, std::uint64_t microseconds) {
    constexpr std::uint64_t microseconds_per_second = 1'000'000;
    auto const whole = microseconds / microseconds_per_second;
    auto fractional = microseconds % microseconds_per_second;
    out += std::to_string(whole);
    if (fractional == 0) {
        return;
    }

    std::array<char, 6> digits{};
    for (std::size_t i = digits.size(); i != 0; --i) {
        digits[i - 1] = static_cast<char>('0' + (fractional % 10));
        fractional /= 10;
    }
    auto last = digits.size();
    while (last != 0 && digits[last - 1] == '0') {
        --last;
    }
    out += '.';
    out.append(digits.data(), last);
}

// 단일 metric을 Prometheus text(HELP/TYPE/value)로 append한다.
void append_metric(
    std::string& out, char const* name, char const* help, char const* type, std::uint64_t value
) {
    append_metric_header(out, name, help, type);
    out += name;
    out += ' ';
    out += std::to_string(value);
    out += '\n';
}

void append_seconds_metric(
    std::string& out, char const* name, char const* help, char const* type, std::uint64_t value_us
) {
    append_metric_header(out, name, help, type);
    out += name;
    out += ' ';
    append_microseconds_as_seconds(out, value_us);
    out += '\n';
}

// Prometheus label value escaping (backslash, double-quote, newline).
void append_label_value(std::string& out, std::string_view value) {
    for (char const c : value) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out += c;
            break;
        }
    }
}

void append_reason_counter(
    std::string& out, char const* name, std::string_view reason, std::uint64_t value
) {
    out += name;
    out += "{reason=\"";
    append_label_value(out, reason);
    out += "\"} ";
    out += std::to_string(value);
    out += '\n';
}

// pool 라벨 gauge 한 줄. 라벨값은 코드 상수라 이스케이프 불필요.
void append_pool_gauge(std::string& out, char const* name, char const* pool, std::uint64_t value) {
    out += name;
    out += "{pool=\"";
    out += pool;
    out += "\"} ";
    out += std::to_string(value);
    out += '\n';
}

// group 라벨 gauge 한 줄 (locale-독립 double 포맷, json writer와 같은 규약).
void append_group_gauge(
    std::string& out, char const* name, std::string const& group, double value
) {
    out += name;
    out += "{group=\"";
    append_label_value(out, group);
    out += "\"} ";
    char buf[32];
    auto const res = std::to_chars(buf, buf + sizeof(buf), value);
    out.append(buf, res.ptr);
    out += '\n';
}

// command RTT를 Prometheus histogram으로 append한다. 내부 us를 seconds bucket/sum으로 바꾼다.
void append_rtt_histogram(
    std::string& out, std::span<std::uint64_t const> bounds_us,
    std::span<std::uint64_t const> buckets, std::uint64_t sum_us
) {
    constexpr char name[] = "ddcs_command_rtt_seconds";
    append_metric_header(out, name, "Command dispatch-to-outcome latency in seconds.", "histogram");
    std::uint64_t cumulative = 0;
    for (std::size_t i = 0; i < bounds_us.size(); ++i) {
        cumulative += buckets[i];
        out += name;
        out += "_bucket{le=\"";
        append_microseconds_as_seconds(out, bounds_us[i]);
        out += "\"} ";
        out += std::to_string(cumulative);
        out += '\n';
    }
    cumulative += buckets[bounds_us.size()]; // +Inf 오버플로 -> 전체 관측 수(= count)
    out += name;
    out += "_bucket{le=\"+Inf\"} ";
    out += std::to_string(cumulative);
    out += '\n';
    out += name;
    out += "_sum ";
    append_microseconds_as_seconds(out, sum_us);
    out += '\n';
    out += name;
    out += "_count ";
    out += std::to_string(cumulative);
    out += '\n';
}

} // namespace

std::string MetricsService::scrape() {
    std::string out;

    // session gauges. connections는 handshaking/confirming/active를 모두 포함하고,
    // devices는 DeviceRegistry가 Controller 수명 동안 관리하는 Device 수다.
    append_metric(
        out, "ddcs_connections", "Current session connections across all protocol phases.", "gauge",
        sessions_.size()
    );
    append_metric(
        out, "ddcs_devices", "Devices managed by the Controller in this process lifetime.", "gauge",
        devices_.size()
    );

    // command lifecycle. pending은 현재 gauge, 나머지는 logical command/attempt counter다.
    append_metric(
        out, "ddcs_commands_pending", "Logical commands awaiting a terminal outcome.", "gauge",
        commands_.pending_count()
    );
    append_metric(
        out, "ddcs_commands_dispatched_total",
        "Logical commands whose initial dispatch entered the retry state machine.", "counter",
        commands_.metrics().dispatched_total
    );
    append_metric(
        out, "ddcs_commands_superseded_total",
        "Dispatched logical commands replaced by newer intent for the same device and command "
        "family.",
        "counter", commands_.metrics().superseded_total
    );
    append_metric(
        out, "ddcs_commands_succeeded_total",
        "Dispatched logical commands that received a successful outcome.", "counter",
        commands_.metrics().succeeded_total
    );
    append_metric_header(
        out, "ddcs_commands_failed_total",
        "Dispatched logical commands that reached a terminal failure, partitioned by reason.",
        "counter"
    );
    append_reason_counter(
        out, "ddcs_commands_failed_total", "exhausted", commands_.metrics().failed_exhausted_total
    );
    append_reason_counter(
        out, "ddcs_commands_failed_total", "offline", commands_.metrics().failed_offline_total
    );
    append_reason_counter(
        out, "ddcs_commands_failed_total", "encode_fail",
        commands_.metrics().failed_encode_fail_total
    );
    append_metric_header(
        out, "ddcs_command_dispatch_failures_total",
        "Initial command dispatches that did not enter the retry state machine, partitioned by "
        "reason.",
        "counter"
    );
    append_reason_counter(
        out, "ddcs_command_dispatch_failures_total", "offline",
        commands_.metrics().dispatch_failures_offline_total
    );
    append_reason_counter(
        out, "ddcs_command_dispatch_failures_total", "encode_fail",
        commands_.metrics().dispatch_failures_encode_fail_total
    );
    append_metric_header(
        out, "ddcs_command_attempt_failures_total",
        "Failed command attempts that may still retry, partitioned by reason.", "counter"
    );
    append_reason_counter(
        out, "ddcs_command_attempt_failures_total", "agent_failure",
        commands_.metrics().attempt_failures_agent_failure_total
    );
    append_reason_counter(
        out, "ddcs_command_attempt_failures_total", "timeout",
        commands_.metrics().attempt_failures_timeout_total
    );
    append_metric(
        out, "ddcs_command_resends_total",
        "Retry transmissions accepted by the command transport adapter.", "counter",
        commands_.metrics().resends_total
    );
    append_metric(
        out, "ddcs_command_stale_responses_total",
        "Responses for closed or superseded logical commands that were ignored.", "counter",
        commands_.metrics().stale_responses_total
    );
    append_rtt_histogram(
        out, device::CommandService::Metrics::rtt_bucket_bounds_us, commands_.metrics().rtt_buckets,
        commands_.metrics().rtt_us_sum
    );

    // tick duration. DurationStats의 정수 us 누산을 Prometheus base unit seconds로 노출한다.
    append_seconds_metric(
        out, "ddcs_tick_duration_seconds", "Work time of the latest Controller tick in seconds.",
        "gauge", sweep_.last_us
    );
    append_seconds_metric(
        out, "ddcs_tick_duration_seconds_max",
        "Maximum Controller tick work time since process start in seconds.", "gauge", sweep_.max_us
    );
    append_seconds_metric(
        out, "ddcs_tick_duration_seconds_total", "Cumulative Controller tick work time in seconds.",
        "counter", sweep_.sum_us
    );
    append_metric(out, "ddcs_ticks_total", "Completed Controller ticks.", "counter", sweep_.count);

    // session counter. 실제 Session이 registry에서 지워질 때 한 번만 센다.
    append_metric(
        out, "ddcs_messages_received_total",
        "Messages arriving at the Controller session layer from agents.", "counter",
        session_service_.metrics().messages_received_total
    );
    append_metric_header(
        out, "ddcs_connections_closed_total",
        "Session connections closed, partitioned by the bounded close reason.", "counter"
    );
    for (auto const reason : transport::port::disconnect_reasons) {
        append_reason_counter(
            out, "ddcs_connections_closed_total", transport::port::to_string(reason),
            session_service_.metrics().connections_closed(reason)
        );
    }

    // 전송 자원 gauges. scrape 시점의 snapshot이며 connection 수에 비례하는 순회는 scrape에만 있다.
    auto const transport = transport_stats_.transport_stats();
    append_metric(
        out, "ddcs_send_queue_messages",
        "Messages waiting in per-connection send queues across all connections.", "gauge",
        transport.tx_queued_messages
    );
    append_metric_header(
        out, "ddcs_pool_slots",
        "Object pool slot capacity; pools grow in chunks and do not shrink.", "gauge"
    );
    append_pool_gauge(out, "ddcs_pool_slots", "connection", transport.connection_pool_capacity);
    append_pool_gauge(out, "ddcs_pool_slots", "message", transport.message_pool_capacity);
    append_metric_header(
        out, "ddcs_pool_slots_acquired", "Object pool slots currently acquired.", "gauge"
    );
    append_pool_gauge(
        out, "ddcs_pool_slots_acquired", "connection", transport.connection_pool_acquired
    );
    append_pool_gauge(out, "ddcs_pool_slots_acquired", "message", transport.message_pool_acquired);

    // per-group gauges. aggregate_groups is the single implementation of policy/metric membership.
    auto const groups = device::aggregate_groups(active_devices_, devices_, policy_);
    append_metric_header(
        out, "ddcs_group_load_ratio",
        "Average reported simulator load for active devices in the group, normalized from 0-100 to "
        "0-1.",
        "gauge"
    );
    for (auto const& [group, aggregate] : groups) {
        double const average =
            aggregate.device_count != 0
                ? aggregate.load_sum / static_cast<double>(aggregate.device_count)
                : 0.0;
        append_group_gauge(out, "ddcs_group_load_ratio", group, average / 100.0);
    }

    append_metric_header(
        out, "ddcs_group_temperature_celsius",
        "Average reported temperature in Celsius for active devices in the group.", "gauge"
    );
    for (auto const& [group, aggregate] : groups) {
        double const average =
            aggregate.device_count != 0
                ? aggregate.temp_sum / static_cast<double>(aggregate.device_count)
                : 0.0;
        append_group_gauge(out, "ddcs_group_temperature_celsius", group, average);
    }

    append_metric_header(
        out, "ddcs_group_devices", "Active devices per group and current mode.", "gauge"
    );
    for (auto const& [group, aggregate] : groups) {
        for (std::uint8_t mode = 0; mode < 3; ++mode) {
            out += "ddcs_group_devices{group=\"";
            append_label_value(out, group);
            out += "\",mode=\"";
            out += ddcs::device::to_string(static_cast<ddcs::device::Mode>(mode));
            out += "\"} ";
            out += std::to_string(aggregate.by_mode[mode]);
            out += '\n';
        }
    }

    return out;
}

} // namespace ddcs::ctrl::app::metrics
