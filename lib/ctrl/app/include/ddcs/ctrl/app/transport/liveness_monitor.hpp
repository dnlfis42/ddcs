#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

#include <chrono>
#include <vector>

#include <cstdint>

namespace ddcs::ctrl::app::transport {

using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;

// active 세션의 liveness 를 주기 sweep 으로 감시 (Redis clientsCron 스타일).
// per-conn 타이머 대신 Session.last_seen + 1초 tick. now - last_seen > timeout 인 active 세션을
// Outbound::close(force) 로 evict. (handshaking=coordinator handshake 타이머, closing=coordinator pw 소관 -
// 여긴 active 만 본다.) 조립 루트가 sweep 직후 coordinator reap 을 구동한다.
class LivenessMonitor {
public:
    LivenessMonitor(
        SessionRegistry& sessions, Outbound& outbound, common::Clock& clock,
        std::chrono::nanoseconds timeout = std::chrono::seconds{3}
    ) noexcept;

    // 주기(1s) 호출. now - last_seen > timeout 인 active 세션을 force close.
    void sweep();

    std::uint64_t evicted_total() const noexcept { return evicted_total_; } // 침묵 evict 누적(알람)

private:
    SessionRegistry& sessions_;
    Outbound& outbound_;
    common::Clock& clock_;
    std::chrono::nanoseconds timeout_;
    std::vector<ConnectionId> stale_; // sweep 재사용 버퍼(순회 중 close 금지 -> 수집 후 처리)
    std::uint64_t evicted_total_{};   // 침묵으로 evict된 active 세션 누적
};

} // namespace ddcs::ctrl::app::transport
