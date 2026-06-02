#pragma once

#include "ddcs/device/mode.hpp"
#include "ddcs/proto/cmd/command.hpp"

namespace ddcs::agent::domain {

// 장치의 현재 상태 = controller 로 보고되는 텔레메트리.
struct DeviceState {
    device::Mode mode;
    double load{}; // 부하 가상지표 0~100 (정책 입력 신호)
    double temp{}; // 온도 C (관측/과열 알람)

    bool operator==(DeviceState const&) const = default;
};

// 장치 추상. agent::app 이 주기 polling 으로 query() 호출.
// query() 는 동기 가정 - timer 콜백 안에서 즉시 반환.
//
// apply(SetMode) 는 디코드된 명령을 적용한다(mode 변경). 명령 종류 판별(CommandType)은
// app 계층이 하고, device 는 이미 해석된 명령만 받는다. 미지원 상태면 false.
class Device {
public:
    virtual ~Device() = default;

    Device(Device const&) = delete;
    Device& operator=(Device const&) = delete;
    Device(Device&&) noexcept = delete;
    Device& operator=(Device&&) noexcept = delete;

    virtual DeviceState query() = 0;
    virtual bool apply(proto::cmd::SetMode const& cmd) = 0;

protected:
    Device() = default;
};

} // namespace ddcs::agent::domain
