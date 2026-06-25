#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/registration_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/connection_listener.hpp"
#include "ddcs/ctrl/app/transport/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/app/transport/port/message_buffer.hpp"
#include "ddcs/ctrl/app/transport/port/message_receiver.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/wire/message/message.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace ddcs::ctrl::app::session {

namespace port = ddcs::ctrl::app::transport::port;

namespace msg = ddcs::wire::message;

// inbound 라우터. 메시지를 decode해 wire 어휘를 use-case 어휘로 번역한다.
// - 연결 수명: on_connected가 SessionRegistry add, on_disconnected가 erase
// - 등록 3-way: RegisterRequest 시 enroll + kick-old + bind + RegisterOutcome 송신,
//   RegisterAck 시 confirm
// - active: Heartbeat/Status/CommandAck/CommandOutcome를 의미 동사로 위임
//           정상 처리된 메시지만 update_seen
// - 단계별 비기대 메시지와 decode 실패는 프로토콜 위반으로 즉시 종료. 등록 실패도 판정 송신 후 종료
class SessionService final : public port::ConnectionListener, public port::MessageReceiver {
public:
    SessionService(
        SessionRegistry& sessions, port::Disconnector& disconnector, port::MessageSender& sender,
        common::Clock& clock, device::RegistrationService& registration_service,
        device::StatusService& status_service, device::CommandService& command_service,
        domain::GroupPolicy const& group_policy
    ) noexcept;

    void on_connected(port::ConnectionId conn) override;
    void on_message(port::ConnectionId conn, port::MessageBuffer payload) override;
    void on_disconnected(port::ConnectionId conn, port::DisconnectReason reason) override;

private:
    void handle_register_request(
        port::ConnectionId conn, std::span<std::byte const> body, common::Clock::time_point now
    );

    void handle_register_ack(
        Session& session, std::span<std::byte const> body, common::Clock::time_point now
    );

    void handle_active_message(
        Session& session, msg::MessageType type, std::span<std::byte const> body,
        common::Clock::time_point now
    );

    bool send_register_outcome(port::ConnectionId conn, bool success);
    // 프로토콜 위반 시 즉시 종료
    void kick(port::ConnectionId conn, std::string_view why);

    SessionRegistry& sessions_;
    port::Disconnector& disconnector_;
    port::MessageSender& sender_;
    common::Clock& clock_;
    device::RegistrationService& registration_service_;
    device::StatusService& status_service_;
    device::CommandService& command_service_;
    domain::GroupPolicy const& group_policy_; // 등록 시 미지 그룹 경고용(읽기 전용)
};

} // namespace ddcs::ctrl::app::session
