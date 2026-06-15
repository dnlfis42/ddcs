#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include <string>

#include <cstdint>

namespace ddcs::ctrl::app::metrics {

namespace {

// 단일 메트릭을 Prometheus text(HELP/TYPE/value)로 append. type은 "gauge" 또는 "counter".
void append_metric(std::string& out, char const* name, char const* help, char const* type, std::uint64_t value) {
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

} // namespace

std::string MetricsService::scrape() {
    std::string out;
    // gauge. 현재값.
    append_metric(out, "ddcs_connections", "Current agent connections (all phases).", "gauge", agents_.size());
    append_metric(out, "ddcs_devices_known", "Persistently known devices (by uuid).", "gauge", devices_.size());
    append_metric(
        out, "ddcs_commands_pending", "In-flight commands awaiting outcome.", "gauge", commands_.pending_count()
    );
    // counter. 누적. 실패율 = gave_up/dispatched, 평균 RTT = rtt_ms_sum/completed.
    append_metric(
        out, "ddcs_commands_dispatched_total", "Commands sent to agents.", "counter", commands_.dispatched_total()
    );
    append_metric(
        out, "ddcs_commands_completed_total", "Commands that received a success outcome.", "counter",
        commands_.completed_total()
    );
    append_metric(
        out, "ddcs_commands_timed_out_total", "Command attempts dropped after no outcome (timeout).", "counter",
        commands_.timed_out_total()
    );
    append_metric(
        out, "ddcs_command_rtt_ms_sum", "Sum of dispatch->outcome latency in ms (avg = /completed_total).", "counter",
        commands_.rtt_ms_sum()
    );
    append_metric(
        out, "ddcs_commands_retried_total", "Command re-sends after timeout/NACK.", "counter", commands_.retried_total()
    );
    append_metric(
        out, "ddcs_commands_superseded_total", "Commands replaced by a newer intent (same device and type).", "counter",
        commands_.superseded_total()
    );
    append_metric(
        out, "ddcs_commands_stale_total", "Late responses to closed/superseded commands (ignored).", "counter",
        commands_.stale_total()
    );
    // 알람 counter. 명령 최종 실패 + agent 건강(operator가 rate로 알람).
    append_metric(
        out, "ddcs_commands_gave_up_total", "Commands abandoned after exhausting retries.", "counter",
        commands_.gave_up_total()
    );
    append_metric(
        out, "ddcs_agents_evicted_total", "Agents force-closed after liveness timeout (unhealthy).", "counter",
        liveness_.evicted_total()
    );
    append_metric(
        out, "ddcs_handshake_expired_total", "Connections dropped for not completing registration in time.", "counter",
        handshake_.expired_total()
    );
    return out;
}

} // namespace ddcs::ctrl::app::metrics
