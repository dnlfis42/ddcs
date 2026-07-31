#pragma once

#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"

namespace ddcs::agent::domain {

// 장치 추상
class Device {
public:
    virtual ~Device() = default;

    Device(Device const&) = delete;
    Device& operator=(Device const&) = delete;
    Device(Device&&) noexcept = delete;
    Device& operator=(Device&&) noexcept = delete;

    virtual common::Uuid id() const = 0; // device 신원 (불변, 등록 시 controller에 제시)
    virtual device::Status query() = 0;
    virtual bool apply(device::Mode mode) = 0;

protected:
    Device() = default;
};

} // namespace ddcs::agent::domain
