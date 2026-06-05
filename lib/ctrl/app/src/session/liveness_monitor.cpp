#include "ddcs/ctrl/app/session/liveness_monitor.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/logger/log.hpp"

#include <chrono>

namespace ddcs::ctrl::app::session {

LivenessMonitor::LivenessMonitor(
    SessionRegistry& sessions, Outbound& outbound, common::Clock& clock, std::chrono::nanoseconds timeout
) noexcept
    : sessions_{sessions}, outbound_{outbound}, clock_{clock}, timeout_{timeout} {}

void LivenessMonitor::sweep() {
    auto const now = clock_.now();
    stale_.clear();
    // 순회 중 sessions_ 변경 금지 -> stale 먼저 수집한 뒤 drop(drop->on_disconnected 가 erase).
    sessions_.for_each([&](ConnectionId conn, Session const& s) {
        if (s.state == State::active && now - s.last_seen > timeout_) {
            stale_.push_back(conn);
            ++evicted_total_;
            auto const silent_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - s.last_seen).count();
            LOG_WARN(
                "agent.unhealthy", ddcs::logger::kv("agent", s.agent.to_string()),
                ddcs::logger::kv("conn", conn.value()), ddcs::logger::kv("silent_ms", silent_ms)
            );
        }
    });
    for (auto const conn : stale_) {
        outbound_.drop(conn);
    }
}

} // namespace ddcs::ctrl::app::session
