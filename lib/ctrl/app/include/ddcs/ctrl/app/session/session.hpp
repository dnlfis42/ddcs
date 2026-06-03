#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/domain/agent/agent_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::session {

using ddcs::ctrl::domain::agent::AgentId;

// 세션 수명 상태.
enum class State : std::uint8_t {
    idle,        // 미사용 초깃값.
    handshaking, // 연결됨, RegisterRequest 대기. 한도는 coordinator의 handshake 타이머(infra)가 감시.
    active,      // 등록 완료, 운영 중. LivenessMonitor가 last_seen으로 침묵을 감시.
    closing,     // graceful close 진행 중. liveness 대상 아님(coordinator pw가 드레인 한도)
};

// 한 연결의 세션 상태(연결<->agent 바인딩 + liveness 관측). SessionRegistry 가 conn 으로 키잉해 소유.
struct Session {
    AgentId agent;                         // active 일 때 유효
    State state{State::idle};              // idle 초깃값
    common::Clock::time_point last_seen{}; // active 트래픽 마지막 관측 시각(liveness)

    // active 트래픽 도착 -> liveness 갱신. (SessionManager 가 active recv 마다 호출)
    void update_seen(common::Clock::time_point now) noexcept { last_seen = now; }
};

} // namespace ddcs::ctrl::app::session
