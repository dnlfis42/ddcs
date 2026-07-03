#pragma once

#include "ddcs/ctrl/app/device/port/command.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

namespace ddcs::ctrl::app::device::port {

// controller에서 agent로 가는 Command 송신 포트
// - device에서 현재 연결로 해소하는 일과 wire 인코딩은 구현 책임이다.
class CommandSender {
public:
    virtual ~CommandSender() = default;

    // best-effort. 미등록/비active device, 인코딩 실패 시 false
    virtual bool
    try_send(domain::DeviceId device, CommandId command_id, Command const& command) = 0;
};

} // namespace ddcs::ctrl::app::device::port
