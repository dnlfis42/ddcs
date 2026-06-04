#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"

#include <unordered_map>
#include <utility>

#include <cstddef>

namespace ddcs::ctrl::app::session {

using ddcs::ctrl::port::transport::ConnectionId;

// 연결<->agent 바인딩 레지스트리(순수 상태). 포트/transport 무지 - 단독 테스트 가능.
//  - forward: conn -> Session
//  - reverse: agent -> 현재 conn (agent->conn 해석, kick-old 추적)
class SessionRegistry {
public:
    // on_connect: handshaking 단계 세션 생성. 이미 있으면 기존 반환(방어).
    Session& open(ConnectionId conn);
    Session* find(ConnectionId conn);

    // 등록: conn 을 agent 에 바인딩(active, last_seen=now). 같은 agent 의 기존 바인딩(다른 conn)이
    // 있으면 그 옛 conn 반환(호출자가 kick). 없으면 ConnectionId{}(무효) 반환.
    ConnectionId bind(ConnectionId conn, DeviceId agent, common::Clock::time_point now);

    // agent -> 현재 conn (없으면 ConnectionId{}).
    ConnectionId resolve(DeviceId agent) const;

    // on_disconnect: 세션 제거. reverse 맵은 현재 바인딩이 이 conn 일 때만 정리(kick 보존).
    void erase(ConnectionId conn);

    // LivenessMonitor sweep 용 순회: fn(ConnectionId, Session const&).
    template <class Fn>
    void for_each(Fn&& fn) const {
        for (auto const& [conn, s] : by_conn_) {
            std::forward<Fn>(fn)(conn, s);
        }
    }

    std::size_t size() const noexcept { return by_conn_.size(); }

private:
    std::unordered_map<ConnectionId, Session> by_conn_;
    std::unordered_map<DeviceId, ConnectionId> agent_to_conn_; // 현재 바인딩
};

} // namespace ddcs::ctrl::app::session
