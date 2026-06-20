#include "ddcs/ctrl/app/agent/agent_service.hpp"

#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/logger/log.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace ddcs::ctrl::app::agent {

AgentService::AgentService(
    AgentRegistry& agents, port::MessageSender& sender, port::Disconnector& disconnector,
    common::Clock& clock, device::RegisterService& register_service,
    device::StatusService& status_service, device::CommandService& command_service
) noexcept
    : agents_{agents},
      sender_{sender},
      disconnector_{disconnector},
      clock_{clock},
      register_service_{register_service},
      status_service_{status_service},
      command_service_{command_service} {}

void AgentService::on_connected(port::ConnectionId conn) {
    if (!agents_.add(conn, clock_.now())) {
        // infra 유일성 위반. 버그 신호
        LOG_WARN("agent.connect.duplicate", logger::kv("conn", conn.get()));
        return;
    }
    LOG_INFO("agent.connected", logger::kv("conn", conn.get()));
}

void AgentService::on_disconnected(port::ConnectionId conn, port::DisconnectReason reason) {
    if (agents_.erase(conn)) {
        LOG_INFO(
            "agent.disconnected", logger::kv("conn", conn.get()),
            logger::kv("reason", static_cast<std::uint64_t>(reason))
        );
    }
}

void AgentService::on_message(port::ConnectionId conn, port::MessageBuffer payload) {
    auto const now = clock_.now();
    Agent* const agent = agents_.find(conn);
    if (agent == nullptr) {
        // 종료 직후 잔여. 무해
        LOG_WARN("agent.message.unknown_conn", logger::kv("conn", conn.get()));
        return;
    }

    // payload = acmp `[type][body]`. type를 떼고 body만 핸들러로 넘긴다.
    auto const bytes = payload->data_span();
    if (bytes.empty()) {
        kick(conn, "empty payload"); // acmp 메시지는 최소 type 1바이트
        return;
    }
    acmp::MessageType const type = acmp::message_type(bytes);
    auto const body = bytes.subspan(1);

    switch (agent->state()) {
    case Agent::State::handshaking:
        if (type != acmp::MessageType::register_request) {
            kick(conn, "expected register_request");
            return;
        }
        handle_register_request(conn, body, now);
        return;
    case Agent::State::confirming:
        if (type != acmp::MessageType::register_ack) {
            kick(conn, "expected register_ack");
            return;
        }
        handle_register_ack(*agent, body, now);
        return;
    case Agent::State::active:
        handle_active_message(*agent, type, body, now);
        return;
    case Agent::State::idle:
        kick(conn, "idle agent"); // 불가 경로 방어. registry는 idle을 담지 않는다.
        return;
    }
}

void AgentService::handle_register_request(
    port::ConnectionId conn, std::span<std::byte const> body, common::Clock::time_point now
) {
    auto const request = acmp::decode_register_request(body);
    if (!request) {
        LOG_WARN("agent.register.decode_fail", logger::kv("conn", conn.get()));
        disconnector_.disconnect(conn); // 식별 불가라 응답 없이 종료
        return;
    }
    domain::DeviceId const device = register_service_.enroll(request->uuid, request->group);
    if (!device.valid()) {
        LOG_WARN(
            "agent.register.reject", logger::kv("conn", conn.get()),
            logger::kv("why", "invalid identity")
        );
        send_register_outcome(conn, false);
        disconnector_.disconnect(conn); // 등록 실패라 판정 송신 후 종료
        return;
    }
    // kick-old(new-wins): 점유된 device는 옛 연결을 먼저 비운다.
    if (Agent const* const old = agents_.find(device); old != nullptr) {
        LOG_INFO(
            "agent.kick_old", logger::kv("old_conn", old->conn().get()),
            logger::kv("device", device.to_string())
        );
        // CAUTION: 동기로 on_disconnected가 불리고 erase가 되돌아온다
        disconnector_.disconnect(old->conn());
    }
    if (!agents_.bind(conn, device, now)) {
        // 방어
        LOG_WARN(
            "agent.register.reject", logger::kv("conn", conn.get()),
            logger::kv("why", "bind rejected")
        );
        send_register_outcome(conn, false);
        disconnector_.disconnect(conn);
        return;
    }
    if (!send_register_outcome(conn, true)) {
        disconnector_.disconnect(conn); // 판정 전달 불가라 끊고 처음부터 재시도하는 게 깨끗하다.
        return;
    }
    LOG_INFO(
        "agent.registered", logger::kv("conn", conn.get()), logger::kv("device", device.to_string())
    );
}

void AgentService::handle_register_ack(
    Agent& agent, std::span<std::byte const> body, common::Clock::time_point now
) {
    if (!acmp::decode_register_ack(body)) {
        kick(agent.conn(), "register_ack decode_fail");
        return;
    }
    if (!agent.confirm(now)) {
        kick(agent.conn(), "confirm rejected"); // 방어. 상태는 caller가 보장한다.
        return;
    }
    LOG_INFO(
        "agent.confirmed", logger::kv("conn", agent.conn().get()),
        logger::kv("device", agent.device().to_string())
    );
}

void AgentService::handle_active_message(
    Agent& agent, acmp::MessageType type, std::span<std::byte const> body,
    common::Clock::time_point now
) {
    switch (type) {
    case acmp::MessageType::heartbeat: {
        if (!acmp::decode_heartbeat(body)) {
            kick(agent.conn(), "heartbeat decode_fail");
            return;
        }
        agent.update_seen(now);
        return;
    }
    case acmp::MessageType::status: {
        auto const status = acmp::decode_status(body);
        if (!status) {
            kick(agent.conn(), "status decode_fail");
            return;
        }
        // decode 성공한 status frame은 liveness 신호다.
        // 비유한 telemetry는 StatusService가 twin 갱신만 건너뛴다.
        agent.update_seen(now);
        status_service_.update_status(agent.device(), status->mode, status->load, status->temp);
        return;
    }
    case acmp::MessageType::command_ack: {
        auto const ack = acmp::decode_command_ack(body);
        if (!ack) {
            kick(agent.conn(), "command_ack decode_fail");
            return;
        }
        agent.update_seen(now);
        command_service_.acknowledge(agent.device(), device::port::CommandId{ack->command_id}, now);
        return;
    }
    case acmp::MessageType::command_outcome: {
        auto const outcome = acmp::decode_command_outcome(body);
        if (!outcome) {
            kick(agent.conn(), "command_outcome decode_fail");
            return;
        }
        agent.update_seen(now);
        command_service_.settle(
            agent.device(), device::port::CommandId{outcome->command_id},
            outcome->code == acmp::CommandOutcome::Code::success, {}, now
        );
        return;
    }
    default:
        kick(agent.conn(), "unexpected message"); // 미지 type 또는 방향 위반
        return;
    }
}

bool AgentService::send_register_outcome(port::ConnectionId conn, bool success) {
    auto buf = sender_.make_message_buffer();
    auto const written = acmp::encode_register_outcome(
        buf->tailroom_span(),
        success ? acmp::RegisterOutcome::Code::success : acmp::RegisterOutcome::Code::failed
    );
    if (!written) {
        LOG_WARN("agent.register.encode_fail", logger::kv("conn", conn.get()));
        return false;
    }
    if (!buf->try_commit(*written)) {
        LOG_WARN("agent.register.encode_fail", logger::kv("conn", conn.get()));
        return false;
    }
    sender_.send(conn, std::move(buf));
    return true;
}

void AgentService::kick(port::ConnectionId conn, std::string_view why) {
    LOG_WARN("agent.violation", logger::kv("conn", conn.get()), logger::kv("why", why));
    disconnector_.disconnect(conn); // CAUTION: 동기로 on_disconnected가 불리고 erase가 되돌아온다
}

} // namespace ddcs::ctrl::app::agent
