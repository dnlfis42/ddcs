#include "ddcs/ctrl/app/session/session_manager.hpp"

#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <utility>

#include <cstdint>

namespace ddcs::ctrl::app::session {

using ddcs::ctrl::port::transport::CloseMode;

SessionManager::SessionManager(
    SessionRegistry& sessions, RegisterService& registrar, StatusService& status, CommandService& commands,
    Outbound& outbound, common::Clock& clock
) noexcept
    : sessions_{sessions}, registrar_{registrar}, status_{status}, commands_{commands}, outbound_{outbound},
      clock_{clock} {}

void SessionManager::on_connect(ConnectionId conn) {
    sessions_.open(conn); // handshaking 세션 생성 (register 전까지)
}

void SessionManager::on_recv(ConnectionId conn, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) {
    auto const t = static_cast<proto::msg::Type>(type);
    Session* const s = sessions_.find(conn);

    // handshaking: RegisterRequest 만 허용. 잘못된 첫 프레임은 프로토콜 위반 -> close.
    // (coordinator handshake 타이머가 "프레임 없음"을, 이건 "잘못된 첫 프레임"을 닫아 register-or-die 완성.)
    if (s != nullptr && s->state == State::handshaking && t != proto::msg::Type::RegisterRequest) {
        LOG_WARN(
            "dispatch.pre_register_unexpected", ddcs::logger::kv("conn", conn.value()), ddcs::logger::kv("type", type)
        );
        outbound_.close(conn, CloseMode::force);
        return;
    }

    // active 세션의 *모든* 수신 = 살아있음 신호 -> liveness 갱신.
    if (s != nullptr && s->state == State::active) {
        s->update_seen(clock_.now());
    }

    switch (t) {
    case proto::msg::Type::RegisterRequest:
        registrar_.handle_register(conn, std::move(body));
        break;
    case proto::msg::Type::Heartbeat:
        break; // idle keepalive - 위 update_seen 으로 충분. 별도 처리 없음.
    case proto::msg::Type::Status:
        status_.handle_status(conn, std::move(body));
        break;
    case proto::msg::Type::CommandAck:
        commands_.handle_ack(conn, std::move(body));
        break;
    case proto::msg::Type::CommandOutcome:
        commands_.handle_outcome(conn, std::move(body));
        break;
    default:
        // c->a 전용(RegisterResponse/Command) 또는 미지의 타입 -> 프로토콜 위반.
        LOG_WARN("dispatch.unexpected_type", ddcs::logger::kv("conn", conn.value()), ddcs::logger::kv("type", type));
        outbound_.close(conn, CloseMode::force);
        break;
    }
}

void SessionManager::on_close_request(ConnectionId conn, CloseReason reason) {
    if (Session* const s = sessions_.find(conn); s != nullptr) {
        s->state = State::closing; // liveness 에서 즉시 제외(드레인 한도는 coordinator pw 소관)
    }
    // peer_closed(정상 half-close)는 graceful 드레인, 오류성(conn/protocol)은 force.
    CloseMode const mode = (reason == CloseReason::peer_closed) ? CloseMode::graceful : CloseMode::force;
    outbound_.close(conn, mode);
}

void SessionManager::on_disconnect(ConnectionId conn) {
    sessions_.erase(conn);
    // pending command 는 sweep 타임아웃으로 정리(agent 응답 불가).
}

} // namespace ddcs::ctrl::app::session
