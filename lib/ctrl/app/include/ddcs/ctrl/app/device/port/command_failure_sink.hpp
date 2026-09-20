#pragma once

#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

namespace ddcs::ctrl::app::device::port {

// 첫 송신에 성공한 논리 명령의 최종 실패만 통지한다.
// 명령 성공, 교체, 재시도 대기에는 통지하지 않는다. 첫 전송 실패는 dispatch() 반환값으로 알린다.
class CommandFailureSink {
public:
    virtual ~CommandFailureSink() = default;
    virtual void on_command_failed(domain::DeviceId device, CommandId command) noexcept = 0;
};

} // namespace ddcs::ctrl::app::device::port
