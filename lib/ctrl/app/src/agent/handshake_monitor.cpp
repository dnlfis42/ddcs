#include "ddcs/ctrl/app/agent/handshake_monitor.hpp"

#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/logger/log.hpp"

namespace ddcs::ctrl::app::agent {

HandshakeMonitor::HandshakeMonitor(
    AgentRegistry& registry, port::Disconnector& disconnector, std::chrono::nanoseconds timeout
) noexcept
    : registry_{registry},
      disconnector_{disconnector},
      timeout_{timeout} {}

void HandshakeMonitor::sweep(common::Clock::time_point now) {
    stale_.clear();
    registry_.for_each([&](Agent const& agent) {
        bool const registering =
            agent.state() == Agent::State::handshaking || agent.state() == Agent::State::confirming;
        if (registering && now - agent.last_seen() > timeout_) {
            stale_.push_back(agent.conn());
        }
    });

    for (auto const conn : stale_) {
        LOG_WARN("agent.handshake_timeout", logger::kv("conn", conn.value()));
        disconnector_.disconnect(conn);
        ++expired_total_;
    }
}

} // namespace ddcs::ctrl::app::agent
