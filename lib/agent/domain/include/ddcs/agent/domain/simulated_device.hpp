#pragma once

#include "ddcs/agent/domain/device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"

#include <cstdint>

namespace ddcs::agent::domain {

// 시간 변동 load/temp를 보고하는 가짜 장치.
// mode는 스스로 정하지 않는다. 정책이 SetMode로 정하고, sim은 그에 따른 변화율을 적분할 뿐이다.
class SimulatedDevice : public Device {
public:
    struct Config {
        // load = mode-구동 backlog (초당 변화율). 음수=빠짐, 양수=쌓임. clamp [0,100].
        double load_rate_performance = -4.0;
        double load_rate_normal = 2.0;
        double load_rate_safe = 4.0;
        double load_initial = 50.0;
        double load_noise = 1.0; // 매 query +-load_noise (균등). 0이면 완전 결정적(테스트용)
        // temp = mode-구동 열 (초당 변화율). clamp [temp_ambient, temp_max].
        double temp_rate_performance = 6.0;
        double temp_rate_normal = 0.5;
        double temp_rate_safe = -8.0;
        double temp_initial = 45.0;
        double temp_noise = 0.5;
        double temp_ambient = 35.0;
        double temp_max = 120.0;
        // query 1회가 나타내는 시간(초). status 보고 주기에서 주입.
        // rate가 초당 단위라 보고 주기를 바꿔도 초당 변화량은 유지된다.
        double tick_seconds = 1.0;
        // 매-샘플 noise용 seed (개체별로 다르게 주면 device마다 다른 noise). 0이면 내부 상수.
        std::uint64_t seed = 0;
    };

public:
    explicit SimulatedDevice(
        common::Uuid id = {}, device::Mode initial = device::Mode::normal
    ) noexcept;
    SimulatedDevice(common::Uuid id, device::Mode initial, Config cfg) noexcept;

    common::Uuid id() const override {
        return id_;
    }

    device::Status query() override; // mode-구동 load/temp 적분 후 보고
    bool apply(device::Mode mode) override;

    device::Mode mode() const noexcept {
        return mode_;
    }

private:
    double next_noise() noexcept; // [-1, 1) 균등 (xorshift64)

    common::Uuid id_;
    device::Mode mode_;
    Config cfg_;
    double load_;
    double temp_;
    std::uint64_t rng_state_;
};

} // namespace ddcs::agent::domain
