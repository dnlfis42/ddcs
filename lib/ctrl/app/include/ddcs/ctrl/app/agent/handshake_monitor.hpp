#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/agent_registry.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/disconnector.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace ddcs::ctrl::app::agent {

// 등록 미완(handshaking/confirming) 시한을 주기 sweep으로 감시하는 객체
class HandshakeMonitor {
public:
    HandshakeMonitor(
        AgentRegistry& registry, port::Disconnector& disconnector, std::chrono::nanoseconds timeout
    ) noexcept;

    [[nodiscard]] std::uint64_t expired_total() const noexcept { return expired_total_; } // 시한 초과 누적(알람)

    // now - last_seen > timeout인 handshaking/confirming 연결을 끊는다.
    // NOTE: last_seen은 단계 전이(생성/bind)에서 갱신되므로 단계마다 timeout budget을 한 번씩 받는다.
    // CAUTION: disconnect는 동기로 on_disconnected -> erase를 되부른다. 수집과 처형을 분리할 것.
    void sweep(common::Clock::time_point now);

private:
    AgentRegistry& registry_;
    port::Disconnector& disconnector_;
    std::chrono::nanoseconds timeout_;
    std::vector<port::ConnectionId> stale_; // PERF: sweep 재사용 버퍼
    std::uint64_t expired_total_{};
};

} // namespace ddcs::ctrl::app::agent
