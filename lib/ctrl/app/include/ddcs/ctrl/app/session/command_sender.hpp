#pragma once

#include "ddcs/ctrl/app/device/port/command.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

namespace ddcs::ctrl::app::session {

namespace port = ddcs::ctrl::app::transport::port;

// device::port::CommandSender 어댑터
// - device를 active Session의 현재 연결로 해소하고, 명령을 wire로 인코딩해 송신한다.
class CommandSender final : public device::port::CommandSender {
public:
    CommandSender(SessionRegistry& sessions, port::MessageSender& sender) noexcept;

    // 미등록/비active device, 인코딩 실패 시 false
    [[nodiscard]] bool try_send(
        domain::DeviceId device, device::port::CommandId command_id,
        device::port::Command const& command
    ) override;

private:
    SessionRegistry& sessions_;
    port::MessageSender& sender_;
};

} // namespace ddcs::ctrl::app::session
