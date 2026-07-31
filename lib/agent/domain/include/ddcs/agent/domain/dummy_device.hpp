#pragma once

#include "ddcs/agent/domain/device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"

namespace ddcs::agent::domain {

// 고정 텔레메트리를 보유하는 디버깅/테스트/E2E 골격 검증용 가짜 장치
class DummyDevice : public Device {
public:
    explicit DummyDevice(common::Uuid id = {}, device::Mode initial = device::Mode::safe) noexcept;

    common::Uuid id() const override {
        return id_;
    }

    device::Status query() override;
    bool apply(device::Mode mode) override;

    device::Mode mode() const noexcept {
        return mode_;
    }

    void set_mode(device::Mode mode) noexcept;

    // 시뮬레이션/테스트 주입
    void set_load(double load) noexcept {
        load_ = load;
    }

    void set_temp(double temp) noexcept {
        temp_ = temp;
    }

private:
    common::Uuid id_;
    device::Mode mode_ = device::Mode::safe;
    double load_ = 0.0;
    double temp_ = 0.0;
};

} // namespace ddcs::agent::domain
