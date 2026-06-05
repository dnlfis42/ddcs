#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/agent/register_service.hpp"
#include "ddcs/ctrl/app/agent/status_service.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/inbound.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::session {

using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::app::agent::RegisterService;
using ddcs::ctrl::app::agent::StatusService;
using ddcs::ctrl::port::transport::DisconnectReason;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Inbound;
using ddcs::ctrl::port::transport::Outbound;

// inbound (driving) 포트 구현이자 세션 수명 FSM 의 소유자. infra(transport) 이벤트를 받아
// 세션 상태를 전이시키고 app use-case 로 라우팅한다. frame.type opaque 바이트를 여기서 msg::Type 으로
// 해석(의미는 app 책임). transport 헤더는 모르고 포트로만 통신.
//  - on_connected     : 세션 open(handshaking)
//  - on_recv          : handshaking->Register 만 허용 / active 면 update_seen 후 type -> 서비스 분기
//  - on_disconnecting : session -> closing(liveness 제외)
//  - on_disconnected  : 세션 erase
// RegisterRequest 는 여기서 직접 처리: registrar 로 identity 해소 후 session bind(active)+kick-old+ack.
// (identity = DeviceRegistry/RegisterService, session binding = SessionRegistry/SessionManager.)
class SessionManager final : public Inbound {
public:
    SessionManager(
        SessionRegistry& sessions, RegisterService& registrar, StatusService& status, CommandService& commands,
        Outbound& outbound, common::Clock& clock
    ) noexcept;

public:
    void on_connected(ConnectionId conn) override;
    void on_recv(ConnectionId conn, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) override;
    void on_disconnecting(ConnectionId conn, DisconnectReason reason) override;
    void on_disconnected(ConnectionId conn) override;

public:
    std::uint64_t kicked_total() const noexcept { return kicked_total_; } // same-uuid kick 누적(재연결 churn 알람)

private:
    void handle_register(ConnectionId conn, common::PoolHandle<common::LinearBuffer> body); // resolve+bind+kick+ack

    SessionRegistry& sessions_;
    RegisterService& registrar_;
    StatusService& status_;
    CommandService& commands_;
    Outbound& outbound_;
    common::Clock& clock_;
    std::uint64_t kicked_total_{};
};

} // namespace ddcs::ctrl::app::session
