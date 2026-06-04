#include "ddcs/ctrl/app/agent/register_service.hpp"

#include "ddcs/logger/log.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <utility>

#include <cstdint>

namespace ddcs::ctrl::app::agent {

DeviceId RegisterService::resolve(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body) {
    proto::msg::RegisterRequest req{};
    if (!proto::msg::decode(body->readable(), req)) {
        LOG_WARN("agent.register.decode_fail", ddcs::logger::kv("conn", conn.value()));
        return DeviceId{}; // 식별 불가 -> 무효(호출자가 close)
    }
    auto const& agent = registry_.find_or_create(req.agent_uuid);                     // uuid -> 영속 DeviceId
    registry_.set_attributes(agent.id, std::move(req.group), std::move(req.version)); // 선언된 group/version 갱신
    return agent.id;
}

void RegisterService::send_register_response(ConnectionId conn, bool success) {
    auto buf = outbound_.payload_buffer();
    proto::msg::RegisterResponse const resp{
        .result = success ? proto::msg::RegisterResult::success : proto::msg::RegisterResult::failed,
        .reason = {},
    };
    if (!proto::msg::encode(resp, *buf)) {
        return; // 방어 (버퍼 용량 충분)
    }
    outbound_.send(conn, static_cast<std::uint8_t>(proto::msg::type_of<proto::msg::RegisterResponse>), std::move(buf));
}

} // namespace ddcs::ctrl::app::agent
