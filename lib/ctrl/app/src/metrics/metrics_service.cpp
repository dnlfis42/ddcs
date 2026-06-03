#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include <string>

#include <cstdint>

namespace ddcs::ctrl::app::metrics {

namespace {

// 단일 메트릭을 Prometheus text(HELP/TYPE/value)로 append. type 은 "gauge" 또는 "counter".
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
    // gauge - 현재값.
    append_metric(out, "ddcs_sessions", "Current transport sessions (all phases).", "gauge", sessions_.size());
    append_metric(out, "ddcs_agents_registered", "Persistently known agents (by uuid).", "gauge", registry_.size());
    append_metric(
        out, "ddcs_commands_pending", "In-flight commands awaiting outcome.", "gauge", commands_.pending_count()
    );
    // counter - 누적. 실패율 = timed_out/dispatched, 평균 RTT = rtt_ms_sum/completed.
    append_metric(
        out, "ddcs_commands_dispatched_total", "Commands sent to agents.", "counter", commands_.dispatched_total()
    );
    append_metric(
        out, "ddcs_commands_completed_total", "Commands that received an outcome.", "counter",
        commands_.completed_total()
    );
    append_metric(
        out, "ddcs_commands_timed_out_total", "Commands dropped after no outcome (timeout).", "counter",
        commands_.timed_out_total()
    );
    append_metric(
        out, "ddcs_command_rtt_ms_sum", "Sum of dispatch->outcome latency in ms (avg = /completed_total).", "counter",
        commands_.rtt_ms_sum()
    );
    append_metric(
        out, "ddcs_commands_retried_total", "Command re-dispatches after timeout/NACK.", "counter",
        commands_.retried_total()
    );
    // 알람 counter - 명령 최종 실패 + agent 건강(operator 가 rate 로 알람).
    append_metric(
        out, "ddcs_commands_gave_up_total", "Commands abandoned after exhausting retries.", "counter",
        commands_.gave_up_total()
    );
    append_metric(
        out, "ddcs_agents_evicted_total", "Agents force-closed after liveness timeout (unhealthy).", "counter",
        liveness_.evicted_total()
    );
    append_metric(
        out, "ddcs_agents_kicked_total", "Old connections kicked by same-uuid re-register (reconnect churn).",
        "counter", session_manager_.kicked_total()
    );
    return out;
}

} // namespace ddcs::ctrl::app::metrics
