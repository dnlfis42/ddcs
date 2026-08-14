#include "ddcs/agent/app/session/session_service.hpp"

#include "ddcs/device/mode.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/wire/command/command.hpp"
#include "ddcs/wire/message/message.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>

namespace ddcs::agent::app::session {

namespace cmd = ddcs::wire::command;

namespace {

// encoder(span을 받아 쓴 바이트 수 반환)로 MessageBuffer를 채워 commit 후 송신한다.
// - 버퍼 부족이면 disconnect
template <typename Encode>
void emit(Outbound& outbound, msg::MessageType type, Encode&& encode) {
    auto buf = outbound.make_message_buffer();
    auto const written = encode(buf->tailroom_span());
    if (!written || !buf->commit(*written)) {
        // 버퍼 부족은 설계상 불가능(프로그래머 오류). 무언 재연결로 위장되지 않게 출력한다
        LOG_MESSAGE_ENCODE_FAIL(msg::to_string(type));
        outbound.disconnect(DisconnectReason::encode_fail);
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
    LOG_SESSION_CONNECTION_REGISTER_REQUEST();
    send_register_request();
    outbound_.schedule_timer(TimerSlot::register_timeout, cfg_.register_timeout);
}

void SessionService::on_recv(MessageBuffer payload) {
    if (state_ == State::idle) {
        return; // 무시
    }

    auto const bytes = payload->data_span();
    auto const decoded = msg::decode_message(bytes);
    if (!decoded) {
        LOG_MESSAGE_DECODE_FAIL(static_cast<std::uint64_t>(msg::message_type(bytes)));
        outbound_.disconnect(DisconnectReason::bad_message);
        return;
    }

    switch (state_) {
    case State::registering:
        if (auto const* outcome = std::get_if<msg::RegisterOutcome>(&*decoded)) {
            handle_register_outcome(*outcome);
        } else {
            LOG_MESSAGE_UNEXPECTED(msg::to_string(msg::message_type(bytes)), "registering");
            outbound_.disconnect(DisconnectReason::unexpected_message);
        }
        break;
    case State::active:
        if (auto const* req = std::get_if<msg::CommandRequest>(&*decoded)) {
            handle_command(*req);
        } else {
            LOG_MESSAGE_UNEXPECTED(msg::to_string(msg::message_type(bytes)), "active");
            outbound_.disconnect(DisconnectReason::unexpected_message);
        }
        break;
    case State::idle:
        break; // 위 가드로 도달하지 않는다
    }
}

void SessionService::on_disconnected() {
    // app 타이머 취소는 Outbound::disconnect() 계약대로 transport가 일괄 수행한다.
    state_ = State::idle;
    // 재연결은 새 세션이다. controller가 재시작하면 command_id를 1부터 재발급할 수 있으므로
    // dedup 기억을 세션 경계에서 비운다.
    last_command_id_ = 0;
    last_command_code_ = msg::CommandOutcome::Code::success;
}

void SessionService::on_timer(TimerSlot id) {
    switch (id) {
    case TimerSlot::register_timeout:
        if (state_ == State::registering) {
            // 무응답이면 끊고 backoff 재연결
            outbound_.disconnect(DisconnectReason::register_timeout);
        }
        break;
    case TimerSlot::heartbeat:
        if (state_ == State::active) {
            send_heartbeat();
        }
        break;
    case TimerSlot::status_report:
        if (state_ == State::active) {
            send_status_report();
        }
        break;
    }
}

void SessionService::handle_register_outcome(msg::RegisterOutcome const& outcome) {
    if (outcome.code != msg::RegisterOutcome::Code::success) {
        // 거부면 backoff 후 재시도
        outbound_.disconnect(DisconnectReason::register_rejected);
        return;
    }

    outbound_.cancel_timer(TimerSlot::register_timeout);
    outbound_.notify_registered(); // 등록 성공 확정: transport reconnect backoff를 base로 리셋
    send_register_ack(); // 3-way: 결과 수신 확인. controller는 이 시점부터 liveness 측정
    enter_active();
    LOG_SESSION_CONNECTION_REGISTER_SUCCESS(device_.id().to_string());
}

void SessionService::handle_command(msg::CommandRequest const& req) {
    // dedup: 같은 command_id 면 apply 없이 이전 응답 재송신.
    if (req.command_id != 0 && req.command_id == last_command_id_) {
        LOG_SESSION_COMMAND_DEDUP(req.command_id);
        send_command_ack(req.command_id);
        send_command_outcome(req.command_id, last_command_code_);
        return;
    }

    send_command_ack(req.command_id); // 디코딩 성공 후, apply 전 ACK

    // 실패 사유는 code로 wire에 실어 Controller도 같은 어휘를 쓴다.
    msg::CommandOutcome::Code code = msg::CommandOutcome::Code::success;
    if (static_cast<cmd::CommandType>(req.command_type) == cmd::CommandType::set_mode) {
        if (auto const set_mode = cmd::decode_set_mode(req.payload)) {
            // wire raw u8 -> device mode 어휘. 어휘 밖 값이면 실패로 응답한다.
            if (auto const mode = device::decode_mode(set_mode->mode)) {
                if (!device_.apply(*mode)) {
                    code = msg::CommandOutcome::Code::apply_failed;
                }
            } else {
                code = msg::CommandOutcome::Code::bad_mode;
            }
        } else {
            code = msg::CommandOutcome::Code::bad_payload;
        }
    } else {
        code = msg::CommandOutcome::Code::unknown_type;
    }

    send_command_outcome(req.command_id, code);

    last_command_id_ = req.command_id; // dedup 상태 갱신
    last_command_code_ = code;
    LOG_SESSION_COMMAND_APPLY(
        req.command_id, code == msg::CommandOutcome::Code::success, msg::to_string(code)
    );
}

void SessionService::enter_active() {
    state_ = State::active;
    outbound_.schedule_timer(TimerSlot::heartbeat, cfg_.heartbeat);
    // 등록 직후 초기 텔레메트리 1회 게시 + status 타이머 예약
    // -> 컨트롤러가 5초 갭 동안 빈 Shadow로 판단하지 않도록
    send_status_report();
}

void SessionService::send_register_request() {
    emit(outbound_, msg::MessageType::register_request, [this](std::span<std::byte> out) {
        return msg::encode_register_request(out, device_.id(), cfg_.group);
    });
}

void SessionService::send_register_ack() {
    emit(outbound_, msg::MessageType::register_ack, [](std::span<std::byte> out) {
        return msg::encode_register_ack(out);
    });
}

void SessionService::send_heartbeat() {
    emit(outbound_, msg::MessageType::heartbeat, [](std::span<std::byte> out) {
        return msg::encode_heartbeat(out);
    });
    outbound_.schedule_timer(TimerSlot::heartbeat, cfg_.heartbeat); // 주기 재예약
    LOG_SESSION_CONNECTION_HEARTBEAT();
}

void SessionService::send_status_report() {
    auto const state = device_.query();
    LOG_DEVICE_STATUS(device::to_string(state.mode), state.load, state.temp);
    emit(outbound_, msg::MessageType::status_report, [&state](std::span<std::byte> out) {
        return msg::encode_status_report(
            out, device::encode_mode(state.mode), state.load, state.temp
        );
    });
    outbound_.schedule_timer(TimerSlot::status_report, cfg_.status_report);
}

void SessionService::send_command_ack(std::uint64_t command_id) {
    emit(outbound_, msg::MessageType::command_ack, [command_id](std::span<std::byte> out) {
        return msg::encode_command_ack(out, command_id);
    });
}

void SessionService::send_command_outcome(
    std::uint64_t command_id, msg::CommandOutcome::Code code
) {
    emit(
        outbound_, msg::MessageType::command_outcome,
        [command_id, code](std::span<std::byte> out) {
            return msg::encode_command_outcome(out, command_id, code);
        }
    );
}

} // namespace ddcs::agent::app::session
