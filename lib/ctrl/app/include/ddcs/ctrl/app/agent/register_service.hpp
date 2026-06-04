#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;

// 등록 핸드셰이크의 identity/ack 책임.
// RegisterRequest를 decode 해 uuid->DeviceId(영속)로 해소하고 선언된 group/version을 갱신.
class RegisterService {
public:
    RegisterService(DeviceRegistry& registry, Outbound& outbound) noexcept : registry_{registry}, outbound_{outbound} {}

    // RegisterRequest decode -> DeviceId 해소(+ group/version 갱신). decode 실패 시 무효 DeviceId{}.
    // conn은 decode_fail 로그 식별용.
    DeviceId resolve(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body);

    // RegisterResponse(success/fail) 송신.
    void send_register_response(ConnectionId conn, bool success);

private:
    DeviceRegistry& registry_;
    Outbound& outbound_;
};

} // namespace ddcs::ctrl::app::agent
