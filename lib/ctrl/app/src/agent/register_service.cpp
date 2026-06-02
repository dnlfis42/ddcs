#include "ddcs/ctrl/app/agent/register_service.hpp"

#include "ddcs/logger/log.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <utility>

#include <cstdint>

namespace ddcs::ctrl::app::agent {

using ddcs::ctrl::port::transport::CloseMode;

void RegisterService::handle_register(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body) {
    proto::msg::RegisterRequest req{};
    if (!proto::msg::decode(body->readable(), req)) {
        LOG_WARN("agent.register.decode_fail", ddcs::logger::kv("conn", conn.value()));
        outbound_.close(conn, CloseMode::force); // 식별 불가 -> 응답 없이 종료
        return;
    }

    auto const& agent = registry_.find_or_create(req.agent_uuid);                     // uuid -> 영속 AgentId
    registry_.set_attributes(agent.id, std::move(req.group), std::move(req.version)); // 선언된 group/version 갱신
    ConnectionId const kicked = sessions_.bind(conn, agent.id, clock_.now());         // active(+last_seen) + kick-old

    if (kicked.valid()) {
        ++kicked_total_;
        LOG_INFO(
            "agent.kick_old", ddcs::logger::kv("agent", agent.id.value()), ddcs::logger::kv("old_conn", kicked.value()),
            ddcs::logger::kv("new_conn", conn.value())
        );
        outbound_.close(kicked, CloseMode::force); // 옛 연결 축출
    }

    LOG_INFO("agent.register", ddcs::logger::kv("agent", agent.id.value()), ddcs::logger::kv("conn", conn.value()));
    send_register_response(conn, true);
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
