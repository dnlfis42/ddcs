#include "ddcs/ctrl/app/session/session_service.hpp"

#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/logger/event.hpp"

#include <cstdint>
#include <utility>
#include <variant>

namespace ddcs::ctrl::app::session {

SessionService::SessionService(
    SessionRegistry& sessions, port::Disconnector& disconnector, port::MessageSender& sender,
    common::Clock& clock, device::RegistrationService& registration_service,
    device::StatusService& status_service, device::CommandService& command_service,
    device::port::DeviceReleaseSink& release_sink, domain::GroupPolicy const& group_policy,
    std::chrono::nanoseconds handshake_timeout, std::chrono::nanoseconds liveness_timeout
) noexcept
    : sessions_(sessions),
      disconnector_(disconnector),
      sender_(sender),
      clock_(clock),
      registration_service_(registration_service),
      status_service_(status_service),
      command_service_(command_service),
      release_sink_(release_sink),
      group_policy_(group_policy),
      handshake_timeout_(handshake_timeout),
      liveness_timeout_(liveness_timeout) {}

void SessionService::on_connected(port::ConnectionId conn) {
    if (!sessions_.add(conn, clock_.now())) {
        // infra 유일성 위반. 버그 신호
        LOG_SESSION_CONNECTION_DUPLICATE(conn.get());
        return;
    }
    LOG_SESSION_CONNECTION_CONNECT(conn.get());
}

void SessionService::on_disconnected(port::ConnectionId conn, port::DisconnectReason reason) {
    // erase가 세션을 지우기 전에, 바인딩된(confirming/active) device를 집어둔다.
    domain::DeviceId device{};
    if (Session const* const session = sessions_.find(conn); session != nullptr) {
        device = session->device(); // 미바인딩(handshaking)이면 nil
    }
    if (sessions_.erase(conn)) {
        if (device.valid()) {
            // 세션 끝난 device의 per-device 제어 상태 폐기(재접속 시 stale 명령 방지 + 맵 증식
            // 방지)
            release_sink_.on_device_released(device);
        }
        LOG_SESSION_CONNECTION_DISCONNECT(conn.get(), port::to_string(reason));
    }
}

void SessionService::sweep(common::Clock::time_point now) {
    // CAUTION: disconnect는 동기로 on_disconnected 후 erase를 되부른다. 수집과 처형을 분리한다.
    stale_registering_.clear();
    stale_active_.clear();
    sessions_.for_each([&](Session const& session) {
        if (session.active()) {
            if (now - session.last_seen() > liveness_timeout_) {
                stale_active_.push_back(session.conn());
            }
            return;
        }
        // 등록 단계: last_seen은 단계 전이에서만 갱신되므로 단계마다 budget을 한 번씩 받는다.
        if (now - session.last_seen() > handshake_timeout_) {
            stale_registering_.push_back(session.conn());
        }
    });

    for (auto const conn : stale_registering_) {
        disconnector_.disconnect(conn, port::DisconnectReason::handshake_expired);
        ++metrics_.handshake_expired_total;
    }
    for (auto const conn : stale_active_) {
        disconnector_.disconnect(conn, port::DisconnectReason::liveness_expired);
        ++metrics_.evicted_total;
    }
}

void SessionService::on_message(port::ConnectionId conn, port::MessageBuffer payload) {
    auto const now = clock_.now();
    Session* const session = sessions_.find(conn);
    if (session == nullptr) {
        // 종료 직후 잔여. 무해
        LOG_SESSION_CONNECTION_UNKNOWN(conn.get());
        return;
    }

    auto const decoded = msg::decode_message(payload->data_span());
    if (!decoded) {
        kick(conn, port::DisconnectReason::bad_message); // 빈 payload, 미지 type, 구조 불일치
        return;
    }

    switch (session->state()) {
    case Session::State::handshaking:
        if (auto const* request = std::get_if<msg::RegisterRequest>(&*decoded)) {
            handle_register_request(conn, *request, now);
        } else {
            kick(conn, port::DisconnectReason::unexpected_message);
        }
        return;
    case Session::State::confirming:
        if (std::holds_alternative<msg::RegisterAck>(*decoded)) {
            handle_register_ack(*session, now);
        } else {
            kick(conn, port::DisconnectReason::unexpected_message);
        }
        return;
    case Session::State::active:
        handle_active_message(*session, *decoded, now);
        return;
    }
}

void SessionService::handle_register_request(
    port::ConnectionId conn, msg::RegisterRequest const& request, common::Clock::time_point now
) {
    domain::DeviceId const device = registration_service_.enroll(request.uuid, request.group);
    if (!device.valid()) {
        LOG_SESSION_CONNECTION_REGISTER_REJECT(conn.get(), "invalid identity");
        send_register_outcome(conn, false);
        // 등록 실패라 판정 송신 후 종료
        disconnector_.disconnect(conn, port::DisconnectReason::register_rejected);
        return;
    }
    // 미지 그룹은 soft 처리: 경고만 하고 등록은 계속한다(정책 미적용으로 동작).
    if (!group_policy_.contains(request.group)) {
        LOG_DEVICE_GROUP_UNKNOWN(device.to_string(), request.group);
    }
    // kick-old(new-wins): 점유된 device는 옛 연결을 먼저 비운다.
    if (Session const* const old = sessions_.find(device); old != nullptr) {
        // CAUTION: 동기로 on_disconnected가 불리고 erase가 되돌아온다
        disconnector_.disconnect(old->conn(), port::DisconnectReason::kicked);
    }
    if (!sessions_.bind(conn, device, now)) {
        // 방어
        LOG_SESSION_CONNECTION_REGISTER_REJECT(conn.get(), "bind rejected");
        send_register_outcome(conn, false);
        disconnector_.disconnect(conn, port::DisconnectReason::register_rejected);
        return;
    }
    if (!send_register_outcome(conn, true)) {
        // 판정 전달 불가라 끊고 처음부터 재시도하는 게 깨끗하다.
        disconnector_.disconnect(conn, port::DisconnectReason::register_undelivered);
        return;
    }
    LOG_SESSION_CONNECTION_REGISTER_ACCEPT(conn.get(), device.to_string());
}

void SessionService::handle_register_ack(Session& session, common::Clock::time_point now) {
    if (!session.confirm(now)) {
        // 방어. 상태는 caller가 보장한다.
        kick(session.conn(), port::DisconnectReason::internal_error);
        return;
    }
    LOG_SESSION_CONNECTION_ACTIVE(session.conn().get(), session.device().to_string());
}

void SessionService::handle_active_message(
    Session& session, msg::Message const& message, common::Clock::time_point now
) {
    if (std::holds_alternative<msg::Heartbeat>(message)) {
        session.update_seen(now);
        return;
    }
    if (auto const* status = std::get_if<msg::StatusReport>(&message)) {
        // decode 성공한 status_report frame은 liveness 신호다.
        // 비유한 telemetry는 StatusService가 shadow 갱신만 건너뛴다.
        session.update_seen(now);
        status_service_.update_status(session.device(), status->mode, status->load, status->temp);
        return;
    }
    if (auto const* ack = std::get_if<msg::CommandAck>(&message)) {
        session.update_seen(now);
        command_service_.acknowledge(
            session.device(), device::port::CommandId{ack->command_id}, now
        );
        return;
    }
    if (auto const* outcome = std::get_if<msg::CommandOutcome>(&message)) {
        session.update_seen(now);
        command_service_.settle(
            session.device(), device::port::CommandId{outcome->command_id},
            outcome->code == msg::CommandOutcome::Code::success,
            static_cast<std::uint8_t>(outcome->code), now
        );
        return;
    }
    kick(session.conn(), port::DisconnectReason::unexpected_message); // 방향 위반(CA 전용 type 등)
}

bool SessionService::send_register_outcome(port::ConnectionId conn, bool success) {
    auto buf = sender_.make_message_buffer();
    auto const written = msg::encode_register_outcome(
        buf->tailroom_span(),
        success ? msg::RegisterOutcome::Code::success : msg::RegisterOutcome::Code::failed
    );
    if (!written || !buf->commit(*written)) {
        LOG_MESSAGE_ENCODE_FAIL(msg::to_string(msg::MessageType::register_outcome));
        return false;
    }
    sender_.send(conn, std::move(buf));
    return true;
}

void SessionService::kick(port::ConnectionId conn, port::DisconnectReason reason) {
    // CAUTION: 동기로 on_disconnected가 불리고 erase가 되돌아온다.
    disconnector_.disconnect(conn, reason);
}

} // namespace ddcs::ctrl::app::session
