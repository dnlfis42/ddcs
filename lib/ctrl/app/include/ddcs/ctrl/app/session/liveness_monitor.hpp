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

// active 연결의 liveness를 주기 sweep으로 감시하는 객체
class LivenessMonitor {
public:
    LivenessMonitor(
        SessionRegistry& registry, port::Disconnector& disconnector,
        std::chrono::nanoseconds timeout
    ) noexcept;

    [[nodiscard]] std::uint64_t evicted_total() const noexcept {
        return evicted_total_;
    } // 침묵 evict 누적(알람)

    // now - last_seen > timeout인 active 연결을 끊는다.
    //
    // CAUTION: disconnect는 동기로 on_disconnected 후 erase를 되부른다. 수집과 처형을 분리할 것
    void sweep(common::Clock::time_point now);

private:
    SessionRegistry& registry_;
    port::Disconnector& disconnector_;
    std::chrono::nanoseconds timeout_;
    std::vector<port::ConnectionId> stale_; // PERF: sweep 재사용 버퍼
    std::uint64_t evicted_total_ = 0;
};

} // namespace ddcs::ctrl::app::session
