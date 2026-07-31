#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"

namespace ddcs::ctrl::app::device::port {

// device 세션 종료를 device-control(정책)에 통지하는 포트
class DeviceReleaseSink {
public:
    virtual ~DeviceReleaseSink() = default;

    // device가 active 집합에서 빠질 때(정상 종료 / kick-old / liveness evict) 호출.
    virtual void on_device_released(domain::DeviceId device) = 0;
};

} // namespace ddcs::ctrl::app::device::port
