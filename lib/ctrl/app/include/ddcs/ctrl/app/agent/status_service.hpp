#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::agent::AgentRegistry;
using ddcs::ctrl::port::transport::ConnectionId;

// 텔레메트리(Status) 소비 use-case. status_json(JSON) 파싱 -> conn->agent 해소 -> Agent telemetry 갱신.
// 비활성 conn/decode 실패/malformed JSON 은 비치명적으로 드롭.
class StatusService {
public:
    StatusService(SessionRegistry& sessions, AgentRegistry& registry) noexcept
        : sessions_{sessions}, registry_{registry} {}

    void handle_status(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body);

private:
    SessionRegistry& sessions_;
    AgentRegistry& registry_;
};

} // namespace ddcs::ctrl::app::agent
