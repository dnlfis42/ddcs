#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/ctrl/app/agent/agent_registry.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/connection_observer.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/disconnector.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"
#include "ddcs/ctrl/app/agent/port/message_sender.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/register_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/wire/acmp/message.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace ddcs::ctrl::app::agent {

namespace acmp = ddcs::wire::acmp;

// inbound 라우터. acmp 메시지를 decode해 wire 어휘를 use-case 어휘로 번역한다.
// - 연결 수명: on_connected가 AgentRegistry add, on_disconnected가 erase
// - 등록 3-way: RegisterRequest 시 enroll + kick-old + bind + RegisterOutcome 송신,
//               RegisterAck 시 confirm
// - active: Heartbeat/Status/CommandAck/CommandOutcome를 의미 동사로 위임
//           정상 처리된 메시지만 update_seen
// - 단계별 비기대 메시지와 decode 실패는 프로토콜 위반으로 즉시 종료. 등록 실패도 판정 송신 후 종료
class AgentService final : public port::ConnectionObserver {
public:
    AgentService(
        AgentRegistry& agents, port::MessageSender& sender, port::Disconnector& disconnector,
        common::Clock& clock, device::RegisterService& register_service,
        device::StatusService& status_service, device::CommandService& command_service
    ) noexcept;

    void on_connected(port::ConnectionId conn) override;
    void on_message(port::ConnectionId conn, port::MessageBuffer payload) override;
    void on_disconnected(port::ConnectionId conn, port::DisconnectReason reason) override;

private:
    void handle_register_request(
        port::ConnectionId conn, std::span<std::byte const> body, common::Clock::time_point now
    );
    void handle_register_ack(
        Agent& agent, std::span<std::byte const> body, common::Clock::time_point now
    );
    void handle_active_message(
        Agent& agent, acmp::MessageType type, std::span<std::byte const> body,
        common::Clock::time_point now
    );

    bool send_register_outcome(port::ConnectionId conn, bool success);
    void kick(port::ConnectionId conn, std::string_view why); // 프로토콜 위반 시 즉시 종료

private:
    AgentRegistry& agents_;
    port::MessageSender& sender_;
    port::Disconnector& disconnector_;
    common::Clock& clock_;
    device::RegisterService& register_service_;
    device::StatusService& status_service_;
    device::CommandService& command_service_;
};

} // namespace ddcs::ctrl::app::agent
