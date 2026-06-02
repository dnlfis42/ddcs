#include "ddcs/ctrl/app/session/session_registry.hpp"

namespace ddcs::ctrl::app::session {

Session& SessionRegistry::open(ConnectionId conn) {
    auto [it, inserted] = by_conn_.try_emplace(conn);
    if (inserted) {
        it->second.state = State::handshaking; // idle -> handshaking (등록 대기)
    }
    return it->second;
}

Session* SessionRegistry::find(ConnectionId conn) {
    auto const it = by_conn_.find(conn);
    return it == by_conn_.end() ? nullptr : &it->second;
}

ConnectionId SessionRegistry::bind(ConnectionId conn, AgentId agent, common::Clock::time_point now) {
    ConnectionId old{};
    if (auto const ait = agent_to_conn_.find(agent); ait != agent_to_conn_.end() && ait->second != conn) {
        old = ait->second; // 같은 agent 가 다른 conn 에 묶여 있음 -> kick 대상
    }
    auto& s = by_conn_[conn]; // 정상 경로는 open 후이지만 방어적으로 생성 허용
    s.agent = agent;
    s.state = State::active;
    s.last_seen = now;            // 등록 = 첫 활동
    agent_to_conn_[agent] = conn; // 현재 바인딩 갱신
    return old;
}

ConnectionId SessionRegistry::resolve(AgentId agent) const {
    auto const it = agent_to_conn_.find(agent);
    return it == agent_to_conn_.end() ? ConnectionId{} : it->second;
}

void SessionRegistry::erase(ConnectionId conn) {
    auto const it = by_conn_.find(conn);
    if (it == by_conn_.end()) {
        return;
    }
    AgentId const agent = it->second.agent;
    by_conn_.erase(it);
    // kick 으로 다른 conn 이 가져갔으면 reverse 보존 - 현재 바인딩이 이 conn 일 때만 제거.
    if (agent.valid()) {
        if (auto const ait = agent_to_conn_.find(agent); ait != agent_to_conn_.end() && ait->second == conn) {
            agent_to_conn_.erase(ait);
        }
    }
}

} // namespace ddcs::ctrl::app::session
