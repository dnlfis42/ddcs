#include "ddcs/ctrl/app/session/session_manager.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <cstdint>
#include <utility>

namespace ddcs::ctrl::app::session {

using ddcs::ctrl::domain::DeviceId;

SessionManager::SessionManager(
    SessionRegistry& sessions, RegisterService& registrar, StatusService& status, CommandService& commands,
    Outbound& outbound, common::Clock& clock
) noexcept
    : sessions_{sessions}, registrar_{registrar}, status_{status}, commands_{commands}, outbound_{outbound},
      clock_{clock} {}

void SessionManager::on_connected(ConnectionId conn) {
    sessions_.open(conn); // handshaking 세션 생성 (register 전까지)
}

void SessionManager::on_recv(ConnectionId conn, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) {
    auto const t = static_cast<proto::msg::MessageType>(type);
    Session* const s = sessions_.find(conn);

    // handshaking: RegisterRequest 만 허용. 잘못된 첫 프레임은 프로토콜 위반 -> close.
    // (ConnectionManager handshake 타이머가 "프레임 없음"을, 이건 "잘못된 첫 프레임"을 닫아 register-or-die 완성.)
    if (s != nullptr && s->state == State::handshaking && t != proto::msg::MessageType::register_request) {
        LOG_WARN(
            "dispatch.pre_register_unexpected", ddcs::logger::kv("conn", conn.value()), ddcs::logger::kv("type", type)
        );
        outbound_.drop(conn);
        return;
    }

    // active 세션의 *모든* 수신 = 살아있음 신호 -> liveness 갱신.
    if (s != nullptr && s->state == State::active) {
        s->update_seen(clock_.now());
    }

    switch (t) {
    case proto::msg::MessageType::register_request:
        handle_register(conn, std::move(body));
        break;
    case proto::msg::MessageType::heartbeat:
        break; // idle keepalive - 위 update_seen 으로 충분. 별도 처리 없음.
    case proto::msg::MessageType::status:
        status_.handle_status(conn, std::move(body));
        break;
    case proto::msg::MessageType::command_ack:
        commands_.handle_ack(conn, std::move(body));
        break;
    case proto::msg::MessageType::command_outcome:
        commands_.handle_outcome(conn, std::move(body));
        break;
    default:
        // c->a 전용(RegisterResponse/Command) 또는 미지의 타입 -> 프로토콜 위반.
        LOG_WARN("dispatch.unexpected_type", ddcs::logger::kv("conn", conn.value()), ddcs::logger::kv("type", type));
        outbound_.drop(conn);
        break;
    }
}

void SessionManager::on_disconnecting(ConnectionId conn, DisconnectReason /*reason*/) {
    if (Session* const s = sessions_.find(conn); s != nullptr) {
        s->state = State::closing; // liveness 에서 즉시 제외
    }
}

void SessionManager::on_disconnected(ConnectionId conn) {
    sessions_.erase(conn);
    // pending command 는 sweep 타임아웃으로 정리(agent 응답 불가).
}

// RegisterRequest 처리: registrar 로 identity 해소 -> session bind(active+last_seen) + kick-old -> ack.
void SessionManager::handle_register(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body) {
    DeviceId const agent = registrar_.resolve(conn, std::move(body));
    if (!agent.valid()) {
        outbound_.drop(conn); // 식별 불가 -> 응답 없이 종료
        return;
    }
    ConnectionId const kicked = sessions_.bind(conn, agent, clock_.now()); // active(+last_seen) + kick-old 추적
    if (kicked.valid()) {
        ++kicked_total_;
        LOG_INFO(
            "agent.kick_old", ddcs::logger::kv("agent", agent.to_string()),
            ddcs::logger::kv("old_conn", kicked.value()), ddcs::logger::kv("new_conn", conn.value())
        );
        outbound_.drop(kicked); // 옛 연결 축출
    }
    LOG_INFO("agent.register", ddcs::logger::kv("agent", agent.to_string()), ddcs::logger::kv("conn", conn.value()));
    registrar_.send_register_response(conn, true);
}

} // namespace ddcs::ctrl::app::session
