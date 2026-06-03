#pragma once

#include "ddcs/agent/domain/device.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/proto/cmd/command.hpp"

namespace ddcs::agent::domain {

// 시간 변동 부하를 보고하는 가짜 장치. query() 호출마다 위상을 전진시켜
// load 를 sine 곡선으로 흔든다(정책 엔진 임계 돌파 데모용). temp 는 고정.
// load = baseline + amplitude * sin(phase). 클럭 비의존이라 결정적(테스트 가능).
class SimulatedDevice : public Device {
public:
    struct Config {
        double baseline{55.0};  // 중심 부하
        double amplitude{45.0}; // 진폭 (load 범위 [baseline-amp, baseline+amp])
        double step{0.7};       // query 당 위상 증가 (rad)
        double temp{40.0};      // 고정 온도 C
    };

    explicit SimulatedDevice(device::Mode initial = device::Mode::normal) noexcept;
    SimulatedDevice(device::Mode initial, Config cfg) noexcept;

    DeviceState query() override; // 위상 전진 후 load 계산
    bool apply(proto::cmd::SetMode const& cmd) override;

    device::Mode mode() const noexcept { return mode_; }

private:
    device::Mode mode_;
    Config cfg_;
    double phase_{0.0};
};

} // namespace ddcs::agent::domain
