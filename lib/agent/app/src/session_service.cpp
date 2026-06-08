#include "ddcs/agent/app/session_service.hpp"

#include "ddcs/logger/log.hpp"
#include "ddcs/proto/cmd/command.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace ddcs::agent::app {

namespace {

proto::msg::Status status_of(domain::DeviceState const& state) noexcept {
    return proto::msg::Status{.mode = static_cast<std::uint8_t>(state.mode), .load = state.load, .temp = state.temp};
}

} // namespace

SessionService::SessionService(common::Uuid agent_uuid, Device& device, Outbound& outbound) noexcept
    : SessionService{agent_uuid, device, outbound, Config{}} {}

SessionService::SessionService(common::Uuid agent_uuid, Device& device, Outbound& outbound, Config cfg) noexcept
    : agent_uuid_{agent_uuid}, device_{device}, outbound_{outbound}, cfg_{cfg} {}

void SessionService::on_connected() {
    if (state_ != State::idle) {
        return; // 멱등 가드: 정상 흐름은 idle 에서만 진입
    }
    state_ = State::registering;
    LOG_DEBUG("agent.session.connect");
    send_register_request();
    outbound_.schedule_timer(TimerId::register_timeout, cfg_.register_timeout);
}

void SessionService::on_recv(std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) {
    auto const data = body->readable();
    switch (state_) {
    case State::registering:
        if (static_cast<proto::msg::MessageType>(type) == proto::msg::MessageType::register_response) {
            handle_register_response(data);
        } else {
            LOG_WARN("agent.session.unexpected_registering", ddcs::logger::kv("type", type));
            outbound_.close(); // 예상 못 한 메시지
        }
        break;
    case State::active:
        if (static_cast<proto::msg::MessageType>(type) == proto::msg::MessageType::command) {
            handle_command(data);
        } else {
            LOG_WARN("agent.session.unexpected_active", ddcs::logger::kv("type", type));
            outbound_.close();
        }
        break;
    case State::idle:
    case State::closing:
        break; // 무시
    }
}

void SessionService::on_disconnected() {
    outbound_.cancel_timer(TimerId::register_timeout);
    state_ = State::idle;
    LOG_DEBUG("agent.session.disconnect");
}

void SessionService::on_timer(TimerId id) {
    switch (id) {
    case TimerId::register_timeout:
        if (state_ == State::registering) {
            LOG_WARN("agent.session.register_timeout");
            state_ = State::closing;
            outbound_.close(); // 무응답 -> 끊고 backoff 재연결
        }
        break;
    case TimerId::heartbeat:
        if (state_ == State::active) {
            send_heartbeat();
        }
        break;
    case TimerId::status:
        if (state_ == State::active) {
            send_status();
        }
        break;
    }
}

void SessionService::handle_register_response(std::span<std::byte const> body) {
    proto::msg::RegisterResponse resp{};
    if (!proto::msg::decode(body, resp)) {
        outbound_.close();
        return;
    }
    if (resp.result != proto::msg::RegisterResult::success) {
        outbound_.close(); // 거부 -> backoff 후 재시도
        return;
    }
    outbound_.cancel_timer(TimerId::register_timeout);
    enter_active();
    LOG_INFO("agent.session.registered", ddcs::logger::kv("uuid", agent_uuid_.to_string()));
}

void SessionService::enter_active() {
    state_ = State::active;
    outbound_.schedule_timer(TimerId::heartbeat, cfg_.heartbeat);
    outbound_.schedule_timer(TimerId::status, cfg_.status_update);
}

void SessionService::send_register_request() {
    send_message(proto::msg::RegisterRequest{.id = agent_uuid_, .group = cfg_.group});
}

void SessionService::send_heartbeat() {
    send_message(proto::msg::Heartbeat{});
    outbound_.schedule_timer(TimerId::heartbeat, cfg_.heartbeat); // 주기 재무장
    LOG_DEBUG("agent.session.heartbeat");
}

void SessionService::send_status() {
    auto const status = status_of(device_.query());
    LOG_DEBUG(
        "agent.session.status", ddcs::logger::kv("mode", static_cast<std::uint64_t>(status.mode)),
        ddcs::logger::kv("load", status.load), ddcs::logger::kv("temp", status.temp)
    );
    send_message(status);
    outbound_.schedule_timer(TimerId::status, cfg_.status_update);
}

void SessionService::handle_command(std::span<std::byte const> body) {
    proto::msg::Command cmd{};
    std::span<std::byte const> payload{};
    if (!proto::msg::decode(body, cmd, payload)) {
        LOG_WARN("agent.session.cmd.decode_fail");
        outbound_.close();
        return;
    }

    // dedup: 같은 command_id 면 apply 없이 이전 응답 재송신.
    if (cmd.command_id != 0 && cmd.command_id == last_command_id_) {
        LOG_DEBUG("agent.session.cmd.dedup", ddcs::logger::kv("command_id", cmd.command_id));
        send_command_ack(cmd.command_id);
        send_command_outcome(cmd.command_id, last_command_result_, last_command_reason_);
        return;
    }

    send_command_ack(cmd.command_id); // decode 성공 후, apply 전 ACK

    bool ok = false;
    std::string reason;
    if (static_cast<proto::cmd::CommandType>(cmd.type) == proto::cmd::CommandType::set_mode) {
        proto::cmd::SetMode set_mode{};
        if (proto::cmd::decode(payload, set_mode)) {
            ok = device_.apply(set_mode);
            if (!ok) {
                reason = "apply_failed";
            }
        } else {
            reason = "payload_decode_failed";
        }
    } else {
        reason = "unknown_command_type";
    }

    auto const result = ok ? proto::msg::CommandResult::success : proto::msg::CommandResult::failed;
    send_command_outcome(cmd.command_id, result, reason);

    last_command_id_ = cmd.command_id; // dedup 상태 갱신
    last_command_result_ = result;
    last_command_reason_ = reason;
    LOG_INFO("agent.session.cmd.applied", ddcs::logger::kv("command_id", cmd.command_id), ddcs::logger::kv("ok", ok));
}

void SessionService::send_command_ack(std::uint64_t command_id) {
    send_message(proto::msg::CommandAck{.command_id = command_id});
}

void SessionService::send_command_outcome(
    std::uint64_t command_id, proto::msg::CommandResult result, std::string const& reason
) {
    send_message(proto::msg::CommandOutcome{.command_id = command_id, .result = result, .reason = reason});
}

template <typename T>
void SessionService::send_message(T const& msg) {
    auto buf = outbound_.payload_buffer();
    if (!proto::msg::encode(msg, *buf)) {
        outbound_.close(); // 버퍼 부족(방어)
        return;
    }
    outbound_.send(static_cast<std::uint8_t>(proto::msg::type_of<T>), std::move(buf));
}

} // namespace ddcs::agent::app
