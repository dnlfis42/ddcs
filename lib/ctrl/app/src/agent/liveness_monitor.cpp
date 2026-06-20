#include "ddcs/ctrl/app/agent/liveness_monitor.hpp"

#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/logger/log.hpp"

namespace ddcs::ctrl::app::agent {

LivenessMonitor::LivenessMonitor(
    AgentRegistry& registry, port::Disconnector& disconnector, std::chrono::nanoseconds timeout
) noexcept
    : registry_{registry},
      disconnector_{disconnector},
      timeout_{timeout} {}

void LivenessMonitor::sweep(common::Clock::time_point now) {
    stale_.clear();
    registry_.for_each([&](Agent const& agent) {
        if (agent.state() == Agent::State::active && now - agent.last_seen() > timeout_) {
            stale_.push_back(agent.conn());
        }
    });

    for (auto const conn : stale_) {
        LOG_WARN("agent.liveness_timeout", logger::kv("conn", conn.get()));
        disconnector_.disconnect(conn);
        ++evicted_total_;
    }
}

} // namespace ddcs::ctrl::app::agent
