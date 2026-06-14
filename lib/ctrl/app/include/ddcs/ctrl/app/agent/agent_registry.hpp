#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <cassert>
#include <cstddef>
#include <unordered_map>

namespace ddcs::ctrl::app::agent {

// Agent 집합의 단일 진실 + 집합 불변식의 봉인처
// - conn 유일 (primary 키), device당 바인딩(confirming/active) 연결 최대 1 (역색인 키)
// - 역색인 엔트리 <=> 그 device가 바인딩된 Agent (bind가 만들고 erase가 지운다)
// CAUTION: outbound.disconnect는 동기로 on_disconnected -> erase를 되부른다.
// CAUTION  순회 중 disconnect 금지(희생자 수집 후 순회 밖에서), erase 가능 경로 뒤에는 find 재조회
class AgentRegistry {
public:
    [[nodiscard]] std::size_t size() const noexcept { return agents_.size(); }

    [[nodiscard]] Agent* find(port::ConnectionId conn) {
        auto it = agents_.find(conn);
        return it == agents_.end() ? nullptr : &it->second;
    }

    // 역색인 경유. 바인딩된(confirming/active) 연결만 닿는다. (kick-old가 옛 연결을 찾는 경로)
    [[nodiscard]] Agent* find(domain::DeviceId device) {
        auto it = device_index_.find(device);
        return it == device_index_.end() ? nullptr : find(it->second);
    }

    // monitor sweep용 순회
    // CAUTION: 순회 중 등록/제거 금지
    template <typename F>
    void for_each(F&& fn) const {
        for (auto const& entry : agents_) {
            fn(entry.second);
        }
    }

    // on_connected: handshaking Agent 생성. conn 중복이면 false (infra가 유일성을 보장하므로 버그 신호)
    [[nodiscard]] bool add(port::ConnectionId conn, common::Clock::time_point now) {
        return agents_.try_emplace(conn, conn, now).second;
    }

    // 등록 확정: handshaking -> confirming + 역색인 등재. active 전이는 Agent::confirm(RegisterAck 수신)이 맡는다.
    // 실패(상태 불변): conn 없음 / device 이미 점유 / 비handshaking / nil device.
    // NOTE: 점유 device는 호출자가 먼저 kick으로 비워야 한다(disconnect가 동기로 erase까지 끝낸다).
    [[nodiscard]] bool bind(port::ConnectionId conn, domain::DeviceId device, common::Clock::time_point now) {
        auto it = agents_.find(conn);
        if (it == agents_.end()) {
            return false;
        }
        auto const [index_it, inserted] = device_index_.emplace(device, conn);
        if (!inserted) {
            return false;
        }
        if (!it->second.bind(device, now)) {
            device_index_.erase(index_it); // 역색인 선점 롤백 (비handshaking / nil device)
            return false;
        }
        return true;
    }

    // on_disconnected: Agent 제거. 바인딩됐다면(confirming/active) 역색인도 함께 지운다.
    // 다른 경로로는 역색인이 줄지 않는다.
    bool erase(port::ConnectionId conn) {
        auto it = agents_.find(conn);
        if (it == agents_.end()) {
            return false;
        }
        if (it->second.device().valid()) { // 바인딩됨 <=> device 보유
            assert(device_index_.contains(it->second.device()));
            device_index_.erase(it->second.device());
        }
        agents_.erase(it);
        return true;
    }

private:
    std::unordered_map<port::ConnectionId, Agent> agents_;
    std::unordered_map<domain::DeviceId, port::ConnectionId> device_index_; // active 바인딩만
};

} // namespace ddcs::ctrl::app::agent
