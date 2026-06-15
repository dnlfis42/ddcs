#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent {

// 한 연결과 device의 바인딩(ConnectionId와 DeviceId). AgentRegistry가 conn으로 키잉해 소유한다.
// 수명 = 연결의 수명: on_connected에서 생성되고 on_disconnected에서 파괴된다. 재접속은 새 Agent다.
class Agent {
public:
    enum class State : std::uint8_t {
        idle,        // 미사용 초깃값 (null 표지)
        handshaking, // 연결됨, RegisterRequest 대기. HandshakeMonitor가 감시
        confirming,  // 등록 판정(RegisterOutcome) 송신됨, RegisterAck 대기. HandshakeMonitor가 감시
        active,      // 등록 확인 완료, 운영 중. LivenessMonitor가 last_seen 침묵을 감시
    };

public:
    Agent() = default;
    Agent(port::ConnectionId conn, common::Clock::time_point now) noexcept
        : conn_{conn}, state_{State::handshaking}, last_seen_{now} {}

    [[nodiscard]] port::ConnectionId conn() const noexcept { return conn_; }
    [[nodiscard]] domain::DeviceId device() const noexcept { return device_; } // confirming/active에서 유효
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] common::Clock::time_point last_seen() const noexcept { return last_seen_; }
    [[nodiscard]] bool valid() const noexcept { return state_ != State::idle; }

    // 등록 확정: handshaking에서 confirming으로 전이 + device 점유. 요청 수신도 활동이므로 last_seen을 갱신한다.
    //            RegisterAck 대기 구간이 자기 timeout budget을 새로 받는다.
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

    // 활동 관측 시 liveness 갱신. (AgentService가 정상 수신마다 호출)
    void update_seen(common::Clock::time_point now) noexcept { last_seen_ = now; }

private:
    port::ConnectionId conn_{};
    domain::DeviceId device_{};
    State state_{State::idle};
    // NOTE: active 전에는 update_seen이 없다. last_seen은 단계 전이(생성/bind/confirm)에서만 갱신되며
    // NOTE  HandshakeMonitor가 단계별 deadline 기준으로 쓴다. 임의 메시지로는 연장되지 않는다.
    common::Clock::time_point last_seen_{};
};

} // namespace ddcs::ctrl::app::agent
