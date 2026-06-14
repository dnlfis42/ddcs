#include "ddcs/ctrl/app/agent/agent_service.hpp"

#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/dacp/msg/message.hpp"
#include "ddcs/logger/log.hpp"

#include <cstdint>
#include <string>
#include <utility>

namespace ddcs::ctrl::app::agent {

namespace msg = ddcs::dacp::msg;

namespace {

constexpr std::uint8_t type_byte(msg::MessageType type) noexcept { return static_cast<std::uint8_t>(type); }

} // namespace

AgentService::AgentService(
    AgentRegistry& agents, port::MessageSender& sender, port::Disconnector& disconnector, common::Clock& clock,
    device::RegisterService& register_service, device::StatusService& status_service,
    device::CommandService& command_service
) noexcept
    : agents_{agents}, sender_{sender}, disconnector_{disconnector}, clock_{clock}, register_service_{register_service},
      status_service_{status_service}, command_service_{command_service} {}

void AgentService::on_connected(port::ConnectionId conn) {
    if (!agents_.add(conn, clock_.now())) {
        LOG_WARN("agent.connect.duplicate", logger::kv("conn", conn.value())); // infra 유일성 위반. 버그 신호
        return;
    }
    LOG_INFO("agent.connected", logger::kv("conn", conn.value()));
}

void AgentService::on_disconnected(port::ConnectionId conn, port::DisconnectReason reason) {
    if (agents_.erase(conn)) {
        LOG_INFO(
            "agent.disconnected", logger::kv("conn", conn.value()),
            logger::kv("reason", static_cast<std::uint64_t>(reason))
        );
    }
}

void AgentService::on_message(port::ConnectionId conn, std::uint8_t message_type, port::MessageBuffer body) {
    auto const now = clock_.now();
    Agent* const agent = agents_.find(conn);
    if (agent == nullptr) {
        LOG_WARN("agent.message.unknown_conn", logger::kv("conn", conn.value())); // 종료 직후 잔여. 무해
        return;
    }
    switch (agent->state()) {
    case Agent::State::handshaking:
        if (message_type != type_byte(msg::MessageType::register_request)) {
            kick(conn, "expected register_request");
            return;
        }
        handle_register_request(conn, body->readable(), now);
        return;
    case Agent::State::confirming:
        if (message_type != type_byte(msg::MessageType::register_ack)) {
            kick(conn, "expected register_ack");
            return;
        }
        handle_register_ack(*agent, body->readable(), now);
        return;
    case Agent::State::active:
        handle_active_message(*agent, message_type, body->readable(), now);
        return;
    case Agent::State::idle:
        kick(conn, "idle agent"); // 불가 경로 방어. registry는 idle을 담지 않는다
        return;
    }
}

void AgentService::handle_register_request(
    port::ConnectionId conn, std::span<std::byte const> body, common::Clock::time_point now
) {
    msg::RegisterRequest request{};
    if (!msg::decode(body, request)) {
        LOG_WARN("agent.register.decode_fail", logger::kv("conn", conn.value()));
        disconnector_.disconnect(conn); // 식별 불가 -> 응답 없이 종료
        return;
    }
    domain::DeviceId const device = register_service_.enroll(request.id, request.group);
    if (!device.valid()) {
        LOG_WARN("agent.register.reject", logger::kv("conn", conn.value()), logger::kv("why", "invalid identity"));
        send_register_outcome(conn, false, "invalid identity");
        disconnector_.disconnect(conn); // 등록 실패 -> 판정 송신 후 종료
        return;
    }
    // kick-old(new-wins): 점유된 device는 옛 연결을 먼저 비운다.
    if (Agent const* const old = agents_.find(device); old != nullptr) {
        LOG_INFO(
            "agent.kick_old", logger::kv("old_conn", old->conn().value()), logger::kv("device", device.to_string())
        );
        disconnector_.disconnect(old->conn()); // CAUTION: 동기로 on_disconnected -> erase가 되돌아온다
    }
    if (!agents_.bind(conn, device, now)) {
        LOG_WARN("agent.register.reject", logger::kv("conn", conn.value()), logger::kv("why", "bind rejected")); // 방어
        send_register_outcome(conn, false, "bind rejected");
        disconnector_.disconnect(conn);
        return;
    }
    if (!send_register_outcome(conn, true, "")) {
        disconnector_.disconnect(conn); // 판정 전달 불가 -> 끊고 처음부터 재시도가 깨끗하다
        return;
    }
    LOG_INFO("agent.registered", logger::kv("conn", conn.value()), logger::kv("device", device.to_string()));
}

void AgentService::handle_register_ack(Agent& agent, std::span<std::byte const> body, common::Clock::time_point now) {
    msg::RegisterAck ack{};
    if (!msg::decode(body, ack)) {
        kick(agent.conn(), "register_ack decode_fail");
        return;
    }
    if (!agent.confirm(now)) {
        kick(agent.conn(), "confirm rejected"); // 방어. 상태는 caller가 보장한다
        return;
    }
    LOG_INFO(
        "agent.confirmed", logger::kv("conn", agent.conn().value()), logger::kv("device", agent.device().to_string())
    );
}

void AgentService::handle_active_message(
    Agent& agent, std::uint8_t message_type, std::span<std::byte const> body, common::Clock::time_point now
) {
    switch (static_cast<msg::MessageType>(message_type)) {
    case msg::MessageType::heartbeat: {
        msg::Heartbeat heartbeat{};
        if (!msg::decode(body, heartbeat)) {
            kick(agent.conn(), "heartbeat decode_fail");
            return;
        }
        agent.update_seen(now);
        return;
    }
    case msg::MessageType::status: {
        msg::Status status{};
        if (!msg::decode(body, status)) {
            kick(agent.conn(), "status decode_fail");
            return;
        }
        // decode 성공한 status frame은 liveness 신호다. 비유한 telemetry는 StatusService가 twin 갱신만 건너뛴다.
        agent.update_seen(now);
        status_service_.update_status(agent.device(), status.mode, status.load, status.temp);
        return;
    }
    case msg::MessageType::command_ack: {
        msg::CommandAck ack{};
        if (!msg::decode(body, ack)) {
            kick(agent.conn(), "command_ack decode_fail");
            return;
        }
        agent.update_seen(now);
        command_service_.acknowledge(agent.device(), device::port::CommandId{ack.command_id}, now);
        return;
    }
    case msg::MessageType::command_outcome: {
        msg::CommandOutcome outcome{};
        if (!msg::decode(body, outcome)) {
            kick(agent.conn(), "command_outcome decode_fail");
            return;
        }
        agent.update_seen(now);
        command_service_.settle(
            agent.device(), device::port::CommandId{outcome.command_id}, outcome.result == msg::CommandResult::success,
            outcome.reason, now
        );
        return;
    }
    default:
        kick(agent.conn(), "unexpected message"); // 미지 type 또는 방향 위반
        return;
    }
}

bool AgentService::send_register_outcome(port::ConnectionId conn, bool success, std::string_view reason) {
    msg::RegisterOutcome const outcome{
        .result = success ? msg::RegisterResult::success : msg::RegisterResult::failed,
        .reason = std::string{reason},
    };
    auto buf = sender_.make_message_buffer();
    if (!msg::encode(outcome, *buf)) {
        LOG_WARN("agent.register.encode_fail", logger::kv("conn", conn.value()));
        return false;
    }
    sender_.send(conn, type_byte(msg::type_of<msg::RegisterOutcome>), std::move(buf));
    return true;
}

void AgentService::kick(port::ConnectionId conn, std::string_view why) {
    LOG_WARN("agent.violation", logger::kv("conn", conn.value()), logger::kv("why", why));
    disconnector_.disconnect(conn); // CAUTION: 동기로 on_disconnected -> erase가 되돌아온다
}

} // namespace ddcs::ctrl::app::agent
