#pragma once

#include "ddcs/ctrl/app/device/port/command_buffer.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::device::port {

// controller에서 agent로 가는 Command 송신 포트. device에서 현재 연결로 해소하는 일과 헤더 기록은 구현 책임이다.
class CommandSender {
public:
    virtual ~CommandSender() = default;

    // frame/command 헤더 headroom이 예약된 빈 buffer. 호출자는 payload만 쓴다.
    virtual CommandBuffer make_command_buffer() = 0;

    // best-effort. command 헤더는 send 시점에 headroom에 제자리 기록된다.
    // RETURN: 미등록/비active device, 헤더 기록 실패 시 false
    // CAUTION: message는 성패와 무관하게 소비된다. 재전송 보관본은 헤더 미기록 상태로 유지할 것.
    virtual bool
    try_send(domain::DeviceId device, CommandId command_id, std::uint8_t command_type, CommandBuffer message) = 0;
};

} // namespace ddcs::ctrl::app::device::port
