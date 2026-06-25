#pragma once

#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"

namespace ddcs::agent::domain {

// Device의 상태 스냅샷 (mode, load, temp)
struct Status {
    device::Mode mode; // 작동 모드
    double load;       // 부하 가상지표 0~100 (정책 입력 신호)
    double temp;       // 온도 C (관측/과열 알람)

    bool operator==(Status const&) const = default;
};

// 장치 추상
class Device {
public:
    virtual ~Device() = default;

    Device(Device const&) = delete;
    Device& operator=(Device const&) = delete;
    Device(Device&&) noexcept = delete;
    Device& operator=(Device&&) noexcept = delete;

    virtual common::Uuid id() const = 0; // device 신원 (불변, 등록 시 controller에 제시)
    virtual Status query() = 0;
    virtual bool apply(device::Mode mode) = 0;

protected:
    Device() = default;
};

} // namespace ddcs::agent::domain
