#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::agent::AgentRegistry;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;

// 등록 핸드셰이크 use-case. RegisterRequest -> uuid->AgentId 바인딩 + kick-old + RegisterResponse +
// 세션 active(+last_seen). 정체성/세션 수명만 - liveness/telemetry 는 별 use-case 소관.
class RegisterService {
public:
    RegisterService(SessionRegistry& sessions, AgentRegistry& registry, Outbound& outbound, common::Clock& clock) noexcept
        : sessions_{sessions}, registry_{registry}, outbound_{outbound}, clock_{clock} {}

    void handle_register(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body);

    std::uint64_t kicked_total() const noexcept { return kicked_total_; } // same-uuid kick 누적(재연결 churn 알람)

private:
    void send_register_response(ConnectionId conn, bool success);

    SessionRegistry& sessions_;
    AgentRegistry& registry_;
    Outbound& outbound_;
    common::Clock& clock_;
    std::uint64_t kicked_total_{};
};

} // namespace ddcs::ctrl::app::agent
