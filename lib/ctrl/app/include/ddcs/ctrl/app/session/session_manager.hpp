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
using ddcs::ctrl::port::transport::CloseReason;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Inbound;
using ddcs::ctrl::port::transport::Outbound;

// inbound (driving) 포트 구현이자 세션 수명 FSM 의 소유자. infra(transport) 이벤트를 받아
// 세션 상태를 전이시키고 app use-case 로 라우팅한다. frame.type opaque 바이트를 여기서 msg::Type 으로
// 해석(의미는 app 책임). transport 헤더는 모르고 포트로만 통신.
//  - on_connect       : 세션 open(handshaking)
//  - on_recv          : handshaking->Register 만 허용 / active 면 update_seen 후 type -> 서비스 분기
//  - on_close_request : session -> closing(liveness 제외) 후 Outbound::close
//  - on_disconnect    : 세션 erase
class SessionManager final : public Inbound {
public:
    SessionManager(
        SessionRegistry& sessions, RegisterService& registrar, StatusService& status, CommandService& commands,
        Outbound& outbound, common::Clock& clock
    ) noexcept;

    void on_connect(ConnectionId conn) override;
    void on_recv(ConnectionId conn, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) override;
    void on_close_request(ConnectionId conn, CloseReason reason) override;
    void on_disconnect(ConnectionId conn) override;

private:
    SessionRegistry& sessions_;
    RegisterService& registrar_;
    StatusService& status_;
    CommandService& commands_;
    Outbound& outbound_;
    common::Clock& clock_;
};

} // namespace ddcs::ctrl::app::session
