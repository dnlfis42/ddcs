#pragma once

#include "ddcs/ctrl/app/device/port/command_buffer.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::session {

namespace port = ddcs::ctrl::app::transport::port;

// device::port::CommandSender 어댑터
// - device를 active Session의 현재 연결로 해소하고, command 헤더를 headroom에 기록해 송신한다.
class CommandSender final : public device::port::CommandSender {
public:
    CommandSender(SessionRegistry& sessions, port::MessageSender& sender) noexcept;

    // frame(infra) 위에 command 헤더 자리를 적층 예약한 빈 buffer
    device::port::CommandBuffer make_command_buffer() override;

    // 미등록/비active device, 헤더 기록 실패 시 false
    [[nodiscard]] bool try_send(
        domain::DeviceId device, device::port::CommandId command_id, std::uint8_t command_type,
        device::port::CommandBuffer message
    ) override;

private:
    SessionRegistry& sessions_;
    port::MessageSender& sender_;
};

} // namespace ddcs::ctrl::app::session
