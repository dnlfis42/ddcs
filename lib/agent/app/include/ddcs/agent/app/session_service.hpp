#pragma once

#include "ddcs/agent/domain/device.hpp"
#include "ddcs/agent/port/inbound.hpp"
#include "ddcs/agent/port/outbound.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/proto/msg/message.hpp"

#include <chrono>
#include <span>
#include <string>

#include <cstddef>
#include <cstdint>

namespace ddcs::agent::app {

using ddcs::agent::domain::Device;
using ddcs::agent::port::Inbound;
using ddcs::agent::port::Outbound;
using ddcs::agent::port::TimerId;

// agent 측 protocol FSM. Inbound 를 구현하고 Outbound 로 송신한다.
// transport 헤더는 모름 - 포트로만 통신. 단일 연결.
//  (A3a: register. A3b: heartbeat/status. A3c: command.)
class SessionService : public Inbound {
public:
    struct Config {
        std::chrono::nanoseconds heartbeat{std::chrono::seconds{1}};
        std::chrono::nanoseconds status_update{std::chrono::seconds{5}};
        std::chrono::nanoseconds register_timeout{std::chrono::seconds{2}};
        std::string group;   // 등록 시 controller 에 선언할 그룹(정책 타깃팅). 빈 문자열 = 미지정
        std::string version; // 등록 시 controller 에 선언할 agent 버전
    };

    enum class State : std::uint8_t {
        idle,        // 연결 없음
        registering, // RegisterRequest 송신, RegisterResponse 대기
        active,      // 등록 완료
        closing,     // 종료 진행
    };

    SessionService(common::Uuid agent_uuid, Device& device, Outbound& outbound) noexcept;
    SessionService(common::Uuid agent_uuid, Device& device, Outbound& outbound, Config cfg) noexcept;

    // Inbound (transport -> app)
    void on_connected() override;
    void on_recv(std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) override;
    void on_disconnected() override;
    void on_timer(TimerId id) override;

    State state() const noexcept { return state_; }
    common::Uuid const& agent_uuid() const noexcept { return agent_uuid_; }

private:
    void send_register_request();
    void send_heartbeat();
    void send_status();
    void send_command_ack(std::uint64_t command_id);
    void send_command_outcome(std::uint64_t command_id, proto::msg::CommandResult result, std::string const& reason);
    void enter_active();
    void handle_register_response(std::span<std::byte const> body);
    void handle_command(std::span<std::byte const> body);

    template <typename T>
    void send_message(T const& msg); // encode + outbound_.send(type_of<T>, buf)

    common::Uuid agent_uuid_;
    Device& device_;
    Outbound& outbound_;
    Config cfg_;
    State state_{State::idle};

    // 명령 dedup: 같은 command_id 재수신 시 apply 없이 이전 ACK+Outcome 재송신.
    // 0 = 아직 처리한 명령 없음(controller 는 1 부터 발급).
    std::uint64_t last_command_id_{0};
    proto::msg::CommandResult last_command_result_{proto::msg::CommandResult::success};
    std::string last_command_reason_;
};

} // namespace ddcs::agent::app
