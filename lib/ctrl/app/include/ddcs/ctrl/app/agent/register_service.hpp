#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/domain/agent/agent_id.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::domain::agent::AgentId;
using ddcs::ctrl::domain::agent::AgentRegistry;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;

// 등록 핸드셰이크의 identity/ack 책임. RegisterRequest 를 decode 해 uuid->AgentId(영속)로 해소하고
// 선언된 group/version 을 갱신한다. 세션 바인딩(active 전이/kick-old)은 SessionManager 소관 -
// 여긴 정체성 해소와 RegisterResponse 송신만 담당.
class RegisterService {
public:
    RegisterService(AgentRegistry& registry, Outbound& outbound) noexcept : registry_{registry}, outbound_{outbound} {}

    // RegisterRequest decode -> AgentId 해소(+ group/version 갱신). decode 실패 시 무효 AgentId{}.
    // conn 은 decode_fail 로그 식별용.
    AgentId resolve(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body);

    // RegisterResponse(success/fail) 송신.
    void send_register_response(ConnectionId conn, bool success);

private:
    AgentRegistry& registry_;
    Outbound& outbound_;
};

} // namespace ddcs::ctrl::app::agent
