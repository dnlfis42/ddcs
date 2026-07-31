#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::session {

namespace port = ddcs::ctrl::app::transport::port;

// 한 연결과 device의 바인딩 (ConnectionId와 DeviceId)
class Session {
public:
    enum class State : std::uint8_t {
        handshaking, // 연결됨, RegisterRequest 대기
        confirming,  // 등록 판정(RegisterOutcome) 송신됨, RegisterAck 대기
        active,      // 등록 확인 완료, 운영 중. sweep이 last_seen 침묵을 감시
    };

public:
    Session(port::ConnectionId conn, common::Clock::time_point now) noexcept
        : conn_(conn),
          state_(State::handshaking),
          last_seen_(now) {}

    [[nodiscard]] port::ConnectionId conn() const noexcept {
        return conn_;
    }

    // confirming/active에서 유효
    [[nodiscard]] domain::DeviceId device() const noexcept {
        return device_;
    }

    [[nodiscard]] State state() const noexcept {
        return state_;
    }

    [[nodiscard]] bool active() const noexcept {
        return state_ == State::active;
    }

    [[nodiscard]] common::Clock::time_point last_seen() const noexcept {
        return last_seen_;
    }

    // 등록 확정: handshaking에서 confirming으로 전이 + device 점유
    [[nodiscard]] bool bind(domain::DeviceId device, common::Clock::time_point now) noexcept {
        if (state_ != State::handshaking || !device.valid()) {
            return false;
        }
        device_ = device;
        state_ = State::confirming;
        last_seen_ = now;
        return true;
    }

    // 등록 확인: confirming에서 active로 전이. RegisterAck 수신 시점부터 liveness 측정이 시작된다.
    [[nodiscard]] bool confirm(common::Clock::time_point now) noexcept {
        if (state_ != State::confirming) {
            return false;
        }
        state_ = State::active;
        last_seen_ = now;
        return true;
    }

    // 활동 관측 시 liveness 갱신 (SessionService가 정상 수신마다 호출)
    void update_seen(common::Clock::time_point now) noexcept {
        last_seen_ = now;
    }

private:
    port::ConnectionId conn_;
    domain::DeviceId device_;
    State state_;
    // 단계 전이와 active 정상 수신(update_seen)에서만 갱신된다.
    // sweep이 사용한다.
    common::Clock::time_point last_seen_;
};

} // namespace ddcs::ctrl::app::session
