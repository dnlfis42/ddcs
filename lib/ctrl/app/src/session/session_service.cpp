#include "ddcs/ctrl/app/session/session_service.hpp"

#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/logger/log.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

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
        LOG_WARN("session.connect.duplicate", logger::kv("conn", conn.get()));
        return;
    }
    LOG_INFO("session.connected", logger::kv("conn", conn.get()));
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
            release_sink_.on_device_left(device);
        }
        LOG_INFO(
            "session.disconnected", logger::kv("conn", conn.get()),
            logger::kv("reason", static_cast<std::uint64_t>(reason))
        );
    }
}

void SessionService::sweep(common::Clock::time_point now) {
    // CAUTION: disconnect는 동기로 on_disconnected 후 erase를 되부른다. 수집과 처형을 분리한다.
    stale_registering_.clear();
    stale_active_.clear();
    sessions_.for_each([&](Session const& session) {
        if (session.state() == Session::State::active) {
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
        LOG_WARN("session.handshake_timeout", logger::kv("conn", conn.get()));
        disconnector_.disconnect(conn);
        ++metrics_.handshake_expired_total;
    }
    for (auto const conn : stale_active_) {
        LOG_WARN("session.liveness_timeout", logger::kv("conn", conn.get()));
        disconnector_.disconnect(conn);
        ++metrics_.evicted_total;
    }
}

void SessionService::on_message(port::ConnectionId conn, port::MessageBuffer payload) {
    auto const now = clock_.now();
    Session* const session = sessions_.find(conn);
    if (session == nullptr) {
        // 종료 직후 잔여. 무해
        LOG_WARN("session.message.unknown_conn", logger::kv("conn", conn.get()));
        return;
    }

    // payload = msg `[type][body]`. type를 떼고 body만 핸들러로 넘긴다.
    auto const bytes = payload->data_span();
    if (bytes.empty()) {
        kick(conn, "empty payload"); // 메시지는 최소 type 1바이트
        return;
    }
    msg::MessageType const type = msg::message_type(bytes);
    auto const body = bytes.subspan(1);

    switch (session->state()) {
    case Session::State::handshaking:
        if (type != msg::MessageType::register_request) {
            kick(conn, "expected register_request");
            return;
        }
        handle_register_request(conn, body, now);
        return;
    case Session::State::confirming:
        if (type != msg::MessageType::register_ack) {
            kick(conn, "expected register_ack");
            return;
        }
        handle_register_ack(*session, body, now);
        return;
    case Session::State::active:
        handle_active_message(*session, type, body, now);
        return;
    case Session::State::idle:
        kick(conn, "idle session"); // 불가 경로 방어. registry는 idle을 담지 않는다.
        return;
    }
}

void SessionService::handle_register_request(
    port::ConnectionId conn, std::span<std::byte const> body, common::Clock::time_point now
) {
    auto const request = msg::decode_register_request(body);
    if (!request) {
        LOG_WARN("session.register.decode_fail", logger::kv("conn", conn.get()));
        disconnector_.disconnect(conn); // 식별 불가라 응답 없이 종료
        return;
    }
    domain::DeviceId const device = registration_service_.enroll(request->uuid, request->group);
    if (!device.valid()) {
        LOG_WARN(
            "session.register.reject", logger::kv("conn", conn.get()),
            logger::kv("why", "invalid identity")
        );
        send_register_outcome(conn, false);
        disconnector_.disconnect(conn); // 등록 실패라 판정 송신 후 종료
        return;
    }
    // soft 검증:
    // - policy.json에 없는 그룹이면 경고만 하고 등록은 계속한다(정책 미적용 default 모드로 동작).
    // - 오타/그룹 추가 운영에 유연하다.
    if (!group_policy_.contains(request->group)) {
        LOG_WARN(
            "session.register.unknown_group", logger::kv("conn", conn.get()),
            logger::kv("group", request->group)
        );
    }
    // kick-old(new-wins): 점유된 device는 옛 연결을 먼저 비운다.
    if (Session const* const old = sessions_.find(device); old != nullptr) {
        LOG_INFO(
            "session.kick_old", logger::kv("old_conn", old->conn().get()),
            logger::kv("device", device.to_string())
        );
        // CAUTION: 동기로 on_disconnected가 불리고 erase가 되돌아온다
        disconnector_.disconnect(old->conn());
    }
    if (!sessions_.bind(conn, device, now)) {
        // 방어
        LOG_WARN(
            "session.register.reject", logger::kv("conn", conn.get()),
            logger::kv("why", "bind rejected")
        );
        send_register_outcome(conn, false);
        disconnector_.disconnect(conn);
        return;
    }
    if (!send_register_outcome(conn, true)) {
        // 판정 전달 불가라 끊고 처음부터 재시도하는 게 깨끗하다.
        disconnector_.disconnect(conn);
        return;
    }
    LOG_INFO(
        "session.registered", logger::kv("conn", conn.get()),
        logger::kv("device", device.to_string())
    );
}

void SessionService::handle_register_ack(
    Session& session, std::span<std::byte const> body, common::Clock::time_point now
) {
    if (!msg::decode_register_ack(body)) {
        kick(session.conn(), "register_ack decode_fail");
        return;
    }
    if (!session.confirm(now)) {
        // 방어. 상태는 caller가 보장한다.
        kick(session.conn(), "confirm rejected");
        return;
    }
    LOG_INFO(
        "session.confirmed", logger::kv("conn", session.conn().get()),
        logger::kv("device", session.device().to_string())
    );
}

void SessionService::handle_active_message(
    Session& session, msg::MessageType type, std::span<std::byte const> body,
    common::Clock::time_point now
) {
    switch (type) {
    case msg::MessageType::heartbeat: {
        if (!msg::decode_heartbeat(body)) {
            kick(session.conn(), "heartbeat decode_fail");
            return;
        }
        session.update_seen(now);
        return;
    }
    case msg::MessageType::status: {
        auto const status = msg::decode_status(body);
        if (!status) {
            kick(session.conn(), "status decode_fail");
            return;
        }
        // decode 성공한 status frame은 liveness 신호다.
        // 비유한 telemetry는 StatusService가 shadow 갱신만 건너뛴다.
        session.update_seen(now);
        status_service_.update_status(session.device(), status->mode, status->load, status->temp);
        return;
    }
    case msg::MessageType::command_ack: {
        auto const ack = msg::decode_command_ack(body);
        if (!ack) {
            kick(session.conn(), "command_ack decode_fail");
            return;
        }
        session.update_seen(now);
        command_service_.acknowledge(
            session.device(), device::port::CommandId{ack->command_id}, now
        );
        return;
    }
    case msg::MessageType::command_outcome: {
        auto const outcome = msg::decode_command_outcome(body);
        if (!outcome) {
            kick(session.conn(), "command_outcome decode_fail");
            return;
        }
        session.update_seen(now);
        command_service_.settle(
            session.device(), device::port::CommandId{outcome->command_id},
            outcome->code == msg::CommandOutcome::Code::success, {}, now
        );
        return;
    }
    default:
        kick(session.conn(), "unexpected message"); // 미지 type 또는 방향 위반
        return;
    }
}

bool SessionService::send_register_outcome(port::ConnectionId conn, bool success) {
    auto buf = sender_.make_message_buffer();
    auto const written = msg::encode_register_outcome(
        buf->tailroom_span(),
        success ? msg::RegisterOutcome::Code::success : msg::RegisterOutcome::Code::failed
    );
    if (!written) {
        LOG_WARN("session.register.encode_fail", logger::kv("conn", conn.get()));
        return false;
    }
    if (!buf->try_commit(*written)) {
        LOG_WARN("session.register.encode_fail", logger::kv("conn", conn.get()));
        return false;
    }
    sender_.send(conn, std::move(buf));
    return true;
}

void SessionService::kick(port::ConnectionId conn, std::string_view why) {
    LOG_WARN("session.violation", logger::kv("conn", conn.get()), logger::kv("why", why));
    // CAUTION: 동기로 on_disconnected가 불리고 erase가 되돌아온다.
    disconnector_.disconnect(conn);
}

} // namespace ddcs::ctrl::app::session
