#pragma once

#include "ddcs/agent/domain/device.hpp"
#include "ddcs/device/command.hpp"
#include "ddcs/device/mode.hpp"

namespace ddcs::agent::domain {

// 고정 텔레메트리(mode/load/temp)를 보유하는 가짜 장치. 디버깅/테스트/E2E 골격 검증용.
// 시간 기반 변동(sine/noise)이 필요하면 별도 SimulatedDevice 도입(추후 마일스톤).
class DummyDevice : public Device {
public:
    explicit DummyDevice(device::Mode initial = device::Mode::safe) noexcept;

    DeviceState query() override;
    bool apply(device::SetMode const& cmd) override;

    void set_mode(device::Mode mode) noexcept;
    device::Mode mode() const noexcept { return mode_; }
    void set_load(double load) noexcept { load_ = load; } // 시뮬레이션/테스트 주입
    void set_temp(double temp) noexcept { temp_ = temp; }

private:
    device::Mode mode_;
    double load_{};
    double temp_{};
};

} // namespace ddcs::agent::domain
