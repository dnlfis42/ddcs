#include "ddcs/ctrl/app/session/handshake_monitor.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/logger/log.hpp"

namespace ddcs::ctrl::app::session {

HandshakeMonitor::HandshakeMonitor(
    SessionRegistry& registry, port::Disconnector& disconnector, std::chrono::nanoseconds timeout
) noexcept
    : registry_(registry),
      disconnector_(disconnector),
      timeout_(timeout) {}

void HandshakeMonitor::sweep(common::Clock::time_point now) {
    stale_.clear();
    registry_.for_each([&](Session const& session) {
        bool const registering = session.state() == Session::State::handshaking ||
                                 session.state() == Session::State::confirming;
        if (registering && now - session.last_seen() > timeout_) {
            stale_.push_back(session.conn());
        }
    });

    for (auto const conn : stale_) {
        LOG_WARN("session.handshake_timeout", logger::kv("conn", conn.get()));
        disconnector_.disconnect(conn);
        ++expired_total_;
    }
}

} // namespace ddcs::ctrl::app::session
