#pragma once

#include "ddcs/agent/app/transport/port/inbound.hpp"
#include "ddcs/agent/app/transport/port/message_buffer.hpp"
#include "ddcs/agent/app/transport/port/outbound.hpp"
#include "ddcs/agent/domain/device.hpp"
#include "ddcs/wire/message/message.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace ddcs::agent::app::session {

using ddcs::agent::app::transport::port::Inbound;
using ddcs::agent::app::transport::port::MessageBuffer;
using ddcs::agent::app::transport::port::Outbound;
using ddcs::agent::app::transport::port::TimerSlot;
using ddcs::agent::domain::Device;

namespace msg = ddcs::wire::message;

// agent 측 protocol FSM
// - Inbound를 구현하고 Outbound로 송신한다.
// - transport 헤더는 모르고 포트로만 통신한다.
// - 단일 연결 등록 신원은 호스팅하는 Device가 들고 있다(Device::id()).
class SessionService : public Inbound {
public:
    struct Config {
        std::chrono::nanoseconds heartbeat = std::chrono::seconds{1};
        std::chrono::nanoseconds status_update = std::chrono::seconds{5};
        std::chrono::nanoseconds register_timeout = std::chrono::seconds{2};
        std::string group; // 등록 시 controller에 선언할 그룹(정책 타깃팅). 빈 문자열 = 미지정
    };

    enum class State : std::uint8_t {
        idle,        // 연결 없음
        registering, // RegisterRequest 송신, register_outcome 대기
        active,      // 등록 완료(register_ack 송신 후)
        closing,     // 종료 진행
    };

public:
    SessionService(Device& device, Outbound& outbound) noexcept;
    SessionService(Device& device, Outbound& outbound, Config cfg) noexcept;

    // Inbound (transport에서 app으로)
    void on_connected() override;
    void on_recv(MessageBuffer payload) override;
    void on_disconnected() override;
    void on_timer(TimerSlot id) override;

    State state() const noexcept {
        return state_;
    }

private:
    void handle_register_outcome(std::span<std::byte const> body);
    void handle_command(std::span<std::byte const> body);

    void enter_active();

    void send_register_request();
    void send_register_ack();
    void send_heartbeat();
    void send_status();
    void send_command_ack(std::uint64_t command_id);
    void send_command_outcome(std::uint64_t command_id, msg::CommandOutcome::Code code);

    Device& device_;
    Outbound& outbound_;
    Config cfg_;
    State state_ = State::idle;

    // 명령 dedup: 같은 command_id 재수신 시 apply 없이 이전 ACK+Outcome 재송신
    // 0 = 아직 처리한 명령 없음 (controller는 1부터 발급)
    std::uint64_t last_command_id_ = 0;
    // 방어 초기화: last_command_id_==0(미처리)이면 읽히지 않지만, 미초기화 enum 읽기를 원천 차단
    msg::CommandOutcome::Code last_command_code_ = msg::CommandOutcome::Code::success;
};

} // namespace ddcs::agent::app::session
