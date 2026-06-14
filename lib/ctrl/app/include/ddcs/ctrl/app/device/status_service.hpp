#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::device {

// 텔레메트리(Status) 소비 use-case. 보고 값을 device 어휘로 해석해 Device 트윈에 반영한다.
class StatusService {
public:
    explicit StatusService(domain::DeviceRegistry& devices) noexcept : devices_{devices} {}

    // 미지의 mode 값은 safe로 해석한다. 미지의 device는 무시한다.
    // 비유한(NaN/inf) load/temp 보고는 버리고 직전 트윈을 보존한다.
    void update_status(domain::DeviceId id, std::uint8_t mode, double load, double temp);

private:
    domain::DeviceRegistry& devices_;
};

} // namespace ddcs::ctrl::app::device
