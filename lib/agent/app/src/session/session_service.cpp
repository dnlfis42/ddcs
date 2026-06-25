#include "ddcs/agent/app/session/session_service.hpp"

#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/message/command.hpp"
#include "ddcs/wire/message/message.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace ddcs::agent::app::session {

namespace {

// encoder(span을 받아 쓴 바이트 수 반환)로 payload_buffer를 채워 commit 후 송신한다.
// - 버퍼 부족이면 close
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

SessionService::SessionService(Device& device, Outbound& outbound) noexcept
    : SessionService(device, outbound, Config{}) {}

SessionService::SessionService(Device& device, Outbound& outbound, Config cfg) noexcept
    : device_(device),
      outbound_(outbound),
      cfg_(cfg) {}

void SessionService::on_connected() {
    if (state_ != State::idle) {
        return; // 멱등 가드: 정상 흐름은 idle 에서만 진입
    }
    state_ = State::registering;
    LOG_DEBUG("agent.session.connect");
    send_register_request();
    outbound_.schedule_timer(TimerSlot::register_timeout, cfg_.register_timeout);
}

void SessionService::on_recv(MessageBuffer payload) {
    // payload = msg `[type][body]`. type을 떼고 body만 핸들러로 넘긴다.
    auto const bytes = payload->data_span();
    msg::MessageType const type = msg::message_type(bytes);
    auto const body = bytes.empty() ? bytes : bytes.subspan(1);

    switch (state_) {
    case State::registering:
        if (type == msg::MessageType::register_outcome) {
            handle_register_outcome(body);
        } else {
            LOG_WARN(
                "agent.session.unexpected_registering",
                logger::kv("type", static_cast<std::uint64_t>(type))
            );
            outbound_.close(); // 예상 못 한 메시지
        }
        break;
    case State::active:
        if (type == msg::MessageType::command_request) {
            handle_command(body);
        } else {
            LOG_WARN(
                "agent.session.unexpected_active",
                logger::kv("type", static_cast<std::uint64_t>(type))
            );
            outbound_.close();
        }
        break;
    case State::idle:
        [[fallthrough]];
    case State::closing:
        break; // 무시
    }
}

void SessionService::on_disconnected() {
    outbound_.cancel_timer(TimerSlot::register_timeout);
    state_ = State::idle;
    LOG_DEBUG("agent.session.disconnect");
}

void SessionService::on_timer(TimerSlot id) {
    switch (id) {
    case TimerSlot::register_timeout:
        if (state_ == State::registering) {
            LOG_WARN("agent.session.register_timeout");
            state_ = State::closing;
            outbound_.close(); // 무응답이면 끊고 backoff 재연결
        }
        break;
    case TimerSlot::heartbeat:
        if (state_ == State::active) {
            send_heartbeat();
        }
        break;
    case TimerSlot::status:
        if (state_ == State::active) {
            send_status();
        }
        break;
    }
}

void SessionService::handle_register_outcome(std::span<std::byte const> body) {
    auto const outcome = msg::decode_register_outcome(body);
    if (!outcome) {
        outbound_.close();
        return;
    }
    if (outcome->code != msg::RegisterOutcome::Code::success) {
        outbound_.close(); // 거부면 backoff 후 재시도
        return;
    }
    outbound_.cancel_timer(TimerSlot::register_timeout);
    outbound_.notify_registered(); // 등록 성공 확정: transport reconnect backoff를 base로 리셋
    send_register_ack();           // 3-way: 결과 수신 확인. controller는 이 시점부터 liveness 측정
    enter_active();
    LOG_INFO("agent.session.registered", logger::kv("uuid", device_.id().to_string()));
}

void SessionService::handle_command(std::span<std::byte const> body) {
    auto const cmd = msg::decode_command_request(body);
    if (!cmd) {
        LOG_WARN("agent.session.cmd.decode_fail");
        outbound_.close();
        return;
    }

    // dedup: 같은 command_id 면 apply 없이 이전 응답 재송신.
    if (cmd->command_id != 0 && cmd->command_id == last_command_id_) {
        LOG_DEBUG("agent.session.cmd.dedup", logger::kv("command_id", cmd->command_id));
        send_command_ack(cmd->command_id);
        send_command_outcome(cmd->command_id, last_command_code_);
        return;
    }

    send_command_ack(cmd->command_id); // decode 성공 후, apply 전 ACK

    msg::CommandOutcome::Code code = msg::CommandOutcome::Code::success;
    std::string reason; // 로컬 로그 전용. wire에는 code만 나간다.
    if (static_cast<msg::CommandType>(cmd->command_type) == msg::CommandType::set_mode) {
        if (auto const set_mode = msg::decode_set_mode(cmd->payload)) {
            // wire raw u8 -> device mode 어휘. 어휘 밖 값이면 실패로 응답한다.
            if (auto const mode = device::decode_mode(set_mode->mode)) {
                if (!device_.apply(*mode)) {
                    code = msg::CommandOutcome::Code::failed;
                    reason = "apply_failed";
                }
            } else {
                code = msg::CommandOutcome::Code::failed;
                reason = "bad_mode";
            }
        } else {
            code = msg::CommandOutcome::Code::failed;
            reason = "payload_decode_failed";
        }
    } else {
        code = msg::CommandOutcome::Code::failed;
        reason = "unknown_command_type";
    }

    send_command_outcome(cmd->command_id, code);

    last_command_id_ = cmd->command_id; // dedup 상태 갱신
    last_command_code_ = code;
    LOG_INFO(
        "agent.session.cmd.applied", logger::kv("command_id", cmd->command_id),
        logger::kv("ok", code == msg::CommandOutcome::Code::success), logger::kv("reason", reason)
    );
}

void SessionService::enter_active() {
    state_ = State::active;
    outbound_.schedule_timer(TimerSlot::heartbeat, cfg_.heartbeat);
    // 등록 직후 초기 텔레메트리 1회 게시 + status 타이머 무장
    // -> 컨트롤러가 5초 갭 동안 빈 Shadow로 판단하지 않도록
    send_status();
}

void SessionService::send_register_request() {
    emit(outbound_, [this](std::span<std::byte> out) {
        return msg::encode_register_request(out, device_.id(), cfg_.group);
    });
}

void SessionService::send_register_ack() {
    emit(outbound_, [](std::span<std::byte> out) { return msg::encode_register_ack(out); });
}

void SessionService::send_heartbeat() {
    emit(outbound_, [](std::span<std::byte> out) { return msg::encode_heartbeat(out); });
    outbound_.schedule_timer(TimerSlot::heartbeat, cfg_.heartbeat); // 주기 재무장
    LOG_DEBUG("agent.session.heartbeat");
}

void SessionService::send_status() {
    auto const state = device_.query();
    LOG_DEBUG(
        "agent.session.status", logger::kv("mode", static_cast<std::uint64_t>(state.mode)),
        logger::kv("load", state.load), logger::kv("temp", state.temp)
    );
    emit(outbound_, [&state](std::span<std::byte> out) {
        return msg::encode_status(out, device::encode_mode(state.mode), state.load, state.temp);
    });
    outbound_.schedule_timer(TimerSlot::status, cfg_.status_update);
}

void SessionService::send_command_ack(std::uint64_t command_id) {
    emit(outbound_, [command_id](std::span<std::byte> out) {
        return msg::encode_command_ack(out, command_id);
    });
}

void SessionService::send_command_outcome(
    std::uint64_t command_id, msg::CommandOutcome::Code code
) {
    emit(outbound_, [command_id, code](std::span<std::byte> out) {
        return msg::encode_command_outcome(out, command_id, code);
    });
}

} // namespace ddcs::agent::app::session
