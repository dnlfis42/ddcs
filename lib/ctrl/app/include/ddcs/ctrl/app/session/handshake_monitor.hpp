#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace ddcs::ctrl::app::session {

namespace port = ddcs::ctrl::app::transport::port;

// 등록 미완(handshaking/confirming) 시한을 주기 sweep으로 감시하는 객체
class HandshakeMonitor {
public:
    HandshakeMonitor(
        SessionRegistry& registry, port::Disconnector& disconnector,
        std::chrono::nanoseconds timeout
    ) noexcept;

    // 시한 초과 누적 (알람)
    [[nodiscard]] std::uint64_t expired_total() const noexcept {
        return expired_total_;
    }

    // now - last_seen > timeout인 handshaking/confirming 연결을 끊는다.
    // - last_seen은 단계 전이에서 갱신되므로 단계마다 timeout budget을 한 번씩 받는다.
    //
    // CAUTION: disconnect는 동기로 on_disconnected 후 erase를 되부른다. 수집과 처형을 분리할 것
    void sweep(common::Clock::time_point now);

private:
    SessionRegistry& registry_;
    port::Disconnector& disconnector_;
    std::chrono::nanoseconds timeout_;
    std::vector<port::ConnectionId> stale_; // PERF: sweep 재사용 버퍼
    std::uint64_t expired_total_ = 0;
};

} // namespace ddcs::ctrl::app::session
