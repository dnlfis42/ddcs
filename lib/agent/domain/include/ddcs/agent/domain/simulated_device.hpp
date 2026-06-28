#pragma once

#include "ddcs/agent/domain/device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"

#include <cstdint>

namespace ddcs::agent::domain {

// 시간 변동 load/temp를 보고하는 가짜 장치.
// - device는 자기 mode를 정하지 않는다. 정책이 SetMode로 꽂아주고, 이 sim은 "현재 mode면 내
//   load/temp가 초당 이만큼 변한다"만 적분한다(load/temp 둘 다 mode-구동 상태).
//   performance: load 빠짐 + temp 가열 / normal: load 천천히 쌓임 + 미열 / safe: load 빠르게 쌓임 + 냉각.
// - rate는 초당값이고 tick_seconds(= status 보고 주기)로 시간과 결합한다: state += rate * tick.
//   query 빈도를 바꿔도 초당 변화량이 유지된다(sampling과 decouple).
// - clamp가 발산을 막는다(load [0,100], temp [ambient, max]).
// - noise(매 query) 외엔 결정적이라 테스트 가능(noise=0이면 완전 결정적).
class SimulatedDevice : public Device {
public:
    struct Config {
        // load = mode-구동 backlog (초당 변화율). 음수=빠짐, 양수=쌓임. clamp [0,100].
        double load_rate_performance = -4.0;
        double load_rate_normal = 2.0;
        double load_rate_safe = 4.0;
        double load_initial = 50.0;
        double load_noise = 1.0; // 매 query +-load_noise (균등)
        // temp = mode-구동 열 (초당 변화율). clamp [temp_ambient, temp_max].
        double temp_rate_performance = 6.0;
        double temp_rate_normal = 0.5;
        double temp_rate_safe = -8.0;
        double temp_initial = 45.0;
        double temp_noise = 0.5;
        double temp_ambient = 35.0;
        double temp_max = 120.0;
        // query 1회가 나타내는 시간(초). status 보고 주기에서 주입.
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

    Status query() override; // mode-구동 load/temp 적분 후 보고
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
