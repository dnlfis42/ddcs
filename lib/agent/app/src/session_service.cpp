#include "ddcs/agent/app/session_service.hpp"

#include "ddcs/device/command.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/acmp/message.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace ddcs::agent::app {

namespace acmp = ddcs::wire::acmp;

namespace {

// RegisterOutcome/CommandOutcome의 code 약속:
// 0 = success, 그 외 = failed
constexpr std::uint8_t outcome_success{0};
constexpr std::uint8_t outcome_failed{1};

// encoder(span을 받아 쓴 바이트 수 반환)로 payload_buffer를 채워 commit 후 송신한다.
// 버퍼 부족이면 close
template <typename Encode>
void emit(Outbound& outbound, Encode&& encode) {
    auto buf = outbound.payload_buffer();
    auto const written = encode(buf->tailroom_span());
    if (!written) {
        outbound.close(); // 버퍼 부족(방어)
        return;
    }
    if (!buf->try_commit(*written)) {
        outbound.close();
        return;
    }
    outbound.send(std::move(buf));
}

} // namespace

SessionService::SessionService(common::Uuid agent_uuid, Device& device, Outbound& outbound) noexcept
    : SessionService{agent_uuid, device, outbound, Config{}} {}

SessionService::SessionService(
    common::Uuid agent_uuid, Device& device, Outbound& outbound, Config cfg
) noexcept
    : agent_uuid_{agent_uuid},
      device_{device},
      outbound_{outbound},
      cfg_{cfg} {}

void SessionService::on_connected() {
    if (state_ != State::idle) {
        return; // 멱등 가드: 정상 흐름은 idle 에서만 진입
    }
    state_ = State::registering;
    LOG_DEBUG("agent.session.connect");
    send_register_request();
    outbound_.schedule_timer(TimerId::register_timeout, cfg_.register_timeout);
}

void SessionService::on_recv(common::PoolHandle<common::LinearBuffer> payload) {
    // payload = acmp `[type][body]`. type를 떼고 body만 핸들러로 넘긴다.
    auto const bytes = payload->data_span();
    acmp::MessageType const type = acmp::peek_type(bytes);
    auto const body = bytes.empty() ? bytes : bytes.subspan(1);

    switch (state_) {
    case State::registering:
        if (type == acmp::MessageType::register_outcome) {
            handle_register_outcome(body);
        } else {
            LOG_WARN(
                "agent.session.unexpected_registering",
                ddcs::logger::kv("type", static_cast<std::uint64_t>(type))
            );
            outbound_.close(); // 예상 못 한 메시지
        }
        break;
    case State::active:
        if (type == acmp::MessageType::command_request) {
            handle_command(body);
        } else {
            LOG_WARN(
                "agent.session.unexpected_active",
                ddcs::logger::kv("type", static_cast<std::uint64_t>(type))
            );
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
            outbound_.close(); // 무응답이면 끊고 backoff 재연결
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

void SessionService::handle_register_outcome(std::span<std::byte const> body) {
    auto const outcome = acmp::decode_register_outcome(body);
    if (!outcome) {
        outbound_.close();
        return;
    }
    if (outcome->code != outcome_success) {
        outbound_.close(); // 거부면 backoff 후 재시도
        return;
    }
    outbound_.cancel_timer(TimerId::register_timeout);
    send_register_ack(); // 3-way: 결과 수신 확인. controller는 이 시점부터 liveness 측정
    enter_active();
    LOG_INFO("agent.session.registered", ddcs::logger::kv("uuid", agent_uuid_.to_string()));
}

void SessionService::enter_active() {
    state_ = State::active;
    outbound_.schedule_timer(TimerId::heartbeat, cfg_.heartbeat);
    outbound_.schedule_timer(TimerId::status, cfg_.status_update);
}

void SessionService::send_register_request() {
    emit(outbound_, [this](std::span<std::byte> out) {
        return acmp::encode_register_request(agent_uuid_, cfg_.group, out);
    });
}

void SessionService::send_register_ack() {
    emit(outbound_, [](std::span<std::byte> out) { return acmp::encode_register_ack(out); });
}

void SessionService::send_heartbeat() {
    emit(outbound_, [](std::span<std::byte> out) { return acmp::encode_heartbeat(out); });
    outbound_.schedule_timer(TimerId::heartbeat, cfg_.heartbeat); // 주기 재무장
    LOG_DEBUG("agent.session.heartbeat");
}

void SessionService::send_status() {
    auto const state = device_.query();
    LOG_DEBUG(
        "agent.session.status", ddcs::logger::kv("mode", static_cast<std::uint64_t>(state.mode)),
        ddcs::logger::kv("load", state.load), ddcs::logger::kv("temp", state.temp)
    );
    emit(outbound_, [&state](std::span<std::byte> out) {
        return acmp::encode_status(
            static_cast<std::uint8_t>(state.mode), state.load, state.temp, out
        );
    });
    outbound_.schedule_timer(TimerId::status, cfg_.status_update);
}

void SessionService::handle_command(std::span<std::byte const> body) {
    auto const cmd = acmp::decode_command_request(body);
    if (!cmd) {
        LOG_WARN("agent.session.cmd.decode_fail");
        outbound_.close();
        return;
    }

    // dedup: 같은 command_id 면 apply 없이 이전 응답 재송신.
    if (cmd->command_id != 0 && cmd->command_id == last_command_id_) {
        LOG_DEBUG("agent.session.cmd.dedup", ddcs::logger::kv("command_id", cmd->command_id));
        send_command_ack(cmd->command_id);
        send_command_outcome(cmd->command_id, last_command_code_);
        return;
    }

    send_command_ack(cmd->command_id); // decode 성공 후, apply 전 ACK

    std::uint8_t code = outcome_success;
    std::string reason; // 로컬 로그 전용. wire에는 code만 나간다.
    if (static_cast<device::CommandType>(cmd->command_type) == device::CommandType::set_mode) {
        device::SetMode set_mode{};
        if (device::decode(cmd->payload, set_mode)) {
            if (!device_.apply(set_mode)) {
                code = outcome_failed;
                reason = "apply_failed";
            }
        } else {
            code = outcome_failed;
            reason = "payload_decode_failed";
        }
    } else {
        code = outcome_failed;
        reason = "unknown_command_type";
    }

    send_command_outcome(cmd->command_id, code);

    last_command_id_ = cmd->command_id; // dedup 상태 갱신
    last_command_code_ = code;
    LOG_INFO(
        "agent.session.cmd.applied", ddcs::logger::kv("command_id", cmd->command_id),
        ddcs::logger::kv("ok", code == outcome_success), ddcs::logger::kv("reason", reason)
    );
}

void SessionService::send_command_ack(std::uint64_t command_id) {
    emit(outbound_, [command_id](std::span<std::byte> out) {
        return acmp::encode_command_ack(command_id, out);
    });
}

void SessionService::send_command_outcome(std::uint64_t command_id, std::uint8_t code) {
    emit(outbound_, [command_id, code](std::span<std::byte> out) {
        return acmp::encode_command_outcome(command_id, code, out);
    });
}

} // namespace ddcs::agent::app
