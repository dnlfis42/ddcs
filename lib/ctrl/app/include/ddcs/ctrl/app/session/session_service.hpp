#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/device_release_sink.hpp"
#include "ddcs/ctrl/app/device/registration_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/connection_listener.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/app/transport/port/message_buffer.hpp"
#include "ddcs/ctrl/app/transport/port/message_receiver.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/wire/message/message.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ddcs::ctrl::app::session {

namespace port = ddcs::ctrl::app::transport::port;

namespace msg = ddcs::wire::message;

// inbound 라우터. 메시지를 decode해 wire 어휘를 use-case 어휘로 번역한다.
class SessionService final : public port::ConnectionListener, public port::MessageReceiver {
public:
    SessionService(
        SessionRegistry& sessions, port::Disconnector& disconnector, port::MessageSender& sender,
        common::Clock& clock, device::RegistrationService& registration_service,
        device::StatusService& status_service, device::CommandService& command_service,
        device::port::DeviceReleaseSink& release_sink, domain::GroupPolicy const& group_policy,
        std::chrono::nanoseconds handshake_timeout, std::chrono::nanoseconds liveness_timeout
    ) noexcept;

    void on_connected(port::ConnectionId conn) override;
    void on_disconnected(port::ConnectionId conn, port::DisconnectReason reason) override;

    void on_message(port::ConnectionId conn, port::MessageBuffer payload) override;

    // 시한 초과 세션을 끊는다.
    // 등록 단계(handshaking/confirming)는 handshake budget을 받는다.
    // active는 liveness budget을 받는다.
    void sweep(common::Clock::time_point now);

    // 누적 메트릭(monotonic). close 이유의 cardinality는 DisconnectReason 어휘로 고정한다.
    struct Metrics {
        // 세션 계층에 도착한 수신 메시지(모든 타입, decode 이전). 유입 rate의 분자로,
        // sweep 여유와 함께 봐야 단일 스레드의 포화 여부를 판정할 수 있다.
        std::uint64_t messages_received_total{};
        std::array<std::uint64_t, port::disconnect_reason_count> connections_closed_total{};

        [[nodiscard]] std::uint64_t connections_closed(port::DisconnectReason reason
        ) const noexcept {
            auto const index = port::disconnect_reason_index(reason);
            return index < connections_closed_total.size() ? connections_closed_total[index] : 0;
        }
    };

    [[nodiscard]] Metrics const& metrics() const noexcept {
        return metrics_;
    }

private:
    void handle_register_request(
        port::ConnectionId conn, msg::RegisterRequest const& request, common::Clock::time_point now
    );

    void handle_register_ack(Session& session, common::Clock::time_point now);

    void handle_active_message(
        Session& session, msg::Message const& message, common::Clock::time_point now
    );

    bool send_register_outcome(port::ConnectionId conn, bool success);

    // 프로토콜 위반 시 즉시 종료
    void kick(port::ConnectionId conn, port::DisconnectReason reason);

    SessionRegistry& sessions_;
    port::Disconnector& disconnector_;
    port::MessageSender& sender_;
    common::Clock& clock_;
    device::RegistrationService& registration_service_;
    device::StatusService& status_service_;
    device::CommandService& command_service_;
    device::port::DeviceReleaseSink& release_sink_; // 세션 종료 device의 제어 상태 폐기 통지
    domain::GroupPolicy const& group_policy_; // 등록 시 미지 그룹 경고용 (읽기 전용)
    std::chrono::nanoseconds handshake_timeout_;
    std::chrono::nanoseconds liveness_timeout_;
    std::vector<port::ConnectionId> stale_registering_; // sweep 재사용 버퍼
    std::vector<port::ConnectionId> stale_active_;      // sweep 재사용 버퍼
    Metrics metrics_;
};

} // namespace ddcs::ctrl::app::session
