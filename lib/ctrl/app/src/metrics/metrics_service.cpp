#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include "ddcs/ctrl/app/device/group_aggregate.hpp"
#include "ddcs/device/mode.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ddcs::ctrl::app::metrics {

namespace {

// 단일 메트릭을 Prometheus text(HELP/TYPE/value)로 append. type은 "gauge" 또는 "counter".
void append_metric(
    std::string& out, char const* name, char const* help, char const* type, std::uint64_t value
) {
    out += "# HELP ";
    out += name;
    out += ' ';
    out += help;
    out += "\n# TYPE ";
    out += name;
    out += ' ';
    out += type;
    out += '\n';
    out += name;
    out += ' ';
    out += std::to_string(value);
    out += '\n';
}

// Prometheus label value escaping (backslash, double-quote, newline).
void append_label_value(std::string& out, std::string const& value) {
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

// group 라벨 게이지 한 줄 (locale-독립 double 포맷, json writer와 동일 규약).
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

// command RTT를 Prometheus histogram으로 append (누적 le 버킷 + _sum + _count).
void append_rtt_histogram(
    std::string& out, std::span<std::uint64_t const> bounds_ms,
    std::span<std::uint64_t const> buckets, std::uint64_t sum_ms
) {
    out += "# HELP ddcs_command_rtt_ms Command dispatch->outcome latency in milliseconds.\n";
    out += "# TYPE ddcs_command_rtt_ms histogram\n";
    std::uint64_t cum = 0;
    for (std::size_t i = 0; i < bounds_ms.size(); ++i) {
        cum += buckets[i];
        out += "ddcs_command_rtt_ms_bucket{le=\"";
        out += std::to_string(bounds_ms[i]);
        out += "\"} ";
        out += std::to_string(cum);
        out += '\n';
    }
    cum += buckets[bounds_ms.size()]; // +Inf 오버플로 -> 누적이 곧 전체 관측 수(= count)
    out += "ddcs_command_rtt_ms_bucket{le=\"+Inf\"} ";
    out += std::to_string(cum);
    out += '\n';
    out += "ddcs_command_rtt_ms_sum ";
    out += std::to_string(sum_ms);
    out += '\n';
    out += "ddcs_command_rtt_ms_count ";
    out += std::to_string(cum);
    out += '\n';
}

} // namespace

std::string MetricsService::scrape() {
    std::string out;
    // gauge. 현재값.
    append_metric(
        out, "ddcs_connections", "Current session connections (all phases).", "gauge",
        sessions_.size()
    );
    append_metric(
        out, "ddcs_devices_known", "Persistently known devices (by uuid).", "gauge", devices_.size()
    );
    append_metric(
        out, "ddcs_commands_pending", "In-flight commands awaiting outcome.", "gauge",
        commands_.pending_count()
    );
    // counter. 누적. 실패율 = gave_up/dispatched, 평균 RTT = rtt_ms_sum/completed.
    append_metric(
        out, "ddcs_commands_dispatched_total", "Commands sent to sessions.", "counter",
        commands_.metrics().dispatched_total
    );
    append_metric(
        out, "ddcs_commands_completed_total", "Commands that received a success outcome.",
        "counter", commands_.metrics().completed_total
    );
    append_metric(
        out, "ddcs_commands_timed_out_total",
        "Command attempts dropped after no outcome (timeout).", "counter",
        commands_.metrics().timed_out_total
    );
    append_metric(
        out, "ddcs_commands_retried_total", "Command re-sends after timeout/NACK.", "counter",
        commands_.metrics().retried_total
    );
    append_metric(
        out, "ddcs_commands_superseded_total",
        "Commands replaced by a newer intent (same device and type).", "counter",
        commands_.metrics().superseded_total
    );
    append_metric(
        out, "ddcs_commands_stale_total", "Late responses to closed/superseded commands (ignored).",
        "counter", commands_.metrics().stale_total
    );
    // 알람 counter. 명령 최종 실패 + session 건강 (operator가 rate로 알람)
    append_metric(
        out, "ddcs_commands_gave_up_total", "Commands abandoned after exhausting retries.",
        "counter", commands_.metrics().gave_up_total
    );
    append_metric(
        out, "ddcs_agents_evicted_total", "Agents force-closed after liveness timeout (unhealthy).",
        "counter", session_service_.metrics().evicted_total
    );
    append_metric(
        out, "ddcs_handshake_expired_total",
        "Connections dropped for not completing registration in time.", "counter",
        session_service_.metrics().handshake_expired_total
    );

    // command RTT 분포(histogram). 평균(sum/count)이 숨기는 꼬리(p99 등)를 백분위로 본다.
    append_rtt_histogram(
        out, device::CommandService::Metrics::rtt_bucket_bounds_ms, commands_.metrics().rtt_buckets,
        commands_.metrics().rtt_ms_sum
    );

    // sweep tick 작업 소요(us). 단일 스레드 포화 신호: max/avg가 sweep 주기에 근접하면 한계.
    append_metric(
        out, "ddcs_sweep_duration_us", "Work time of the latest sweep tick in microseconds.",
        "gauge", sweep_.last_us
    );
    append_metric(
        out, "ddcs_sweep_duration_us_max", "Peak sweep tick work time in microseconds since start.",
        "gauge", sweep_.max_us
    );
    append_metric(
        out, "ddcs_sweep_duration_us_sum",
        "Cumulative sweep tick work time in us (avg = /ddcs_sweep_ticks_total).", "counter",
        sweep_.sum_us
    );
    append_metric(
        out, "ddcs_sweep_ticks_total", "Number of sweep ticks executed.", "counter", sweep_.ticks
    );

    // per-group 게이지. active device를 group으로 집계해 group{,mode} 라벨로 노출한다.
    // 정책이 group 단위라(평균 load -> regime -> mode) group별 동작을 분리 관측하고,
    // 집계 규칙은 정책 평가와 같은 aggregate_groups를 공유한다(관측 규칙 = 정책 입력 규칙).
    auto const groups = device::aggregate_groups(roster_, devices_, policy_);

    out += "# HELP ddcs_group_load_avg Average reported load across active devices in the group.\n";
    out += "# TYPE ddcs_group_load_avg gauge\n";
    for (auto const& [group, agg] : groups) {
        double const avg = agg.devices != 0 ? agg.load_sum / static_cast<double>(agg.devices) : 0.0;
        append_group_gauge(out, "ddcs_group_load_avg", group, avg);
    }

    out += "# HELP ddcs_group_temp_avg Average reported temperature (C) across active devices in "
           "the group.\n";
    out += "# TYPE ddcs_group_temp_avg gauge\n";
    for (auto const& [group, agg] : groups) {
        double const avg = agg.devices != 0 ? agg.temp_sum / static_cast<double>(agg.devices) : 0.0;
        append_group_gauge(out, "ddcs_group_temp_avg", group, avg);
    }

    out += "# HELP ddcs_group_devices Active devices per group and mode.\n";
    out += "# TYPE ddcs_group_devices gauge\n";
    for (auto const& [group, agg] : groups) {
        for (std::uint8_t m = 0; m < 3; ++m) {
            out += "ddcs_group_devices{group=\"";
            append_label_value(out, group);
            out += "\",mode=\"";
            out += ddcs::device::to_string(static_cast<ddcs::device::Mode>(m));
            out += "\"} ";
            out += std::to_string(agg.by_mode[m]);
            out += '\n';
        }
    }

    return out;
}

} // namespace ddcs::ctrl::app::metrics
