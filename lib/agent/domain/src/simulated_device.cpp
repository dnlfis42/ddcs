#include "ddcs/agent/domain/simulated_device.hpp"

#include <algorithm>

namespace ddcs::agent::domain {

namespace {

constexpr std::uint64_t default_seed = 0x9e3779b97f4a7c15ULL;

double load_rate(SimulatedDevice::Config const& c, device::Mode m) noexcept {
    switch (m) {
    case device::Mode::performance:
        return c.load_rate_performance;
    case device::Mode::safe:
        return c.load_rate_safe;
    case device::Mode::normal:
        break;
    }
    return c.load_rate_normal;
}

double temp_rate(SimulatedDevice::Config const& c, device::Mode m) noexcept {
    switch (m) {
    case device::Mode::performance:
        return c.temp_rate_performance;
    case device::Mode::safe:
        return c.temp_rate_safe;
    case device::Mode::normal:
        break;
    }
    return c.temp_rate_normal;
}

} // namespace

SimulatedDevice::SimulatedDevice(common::Uuid id, device::Mode initial) noexcept
    : SimulatedDevice(id, initial, Config{}) {}

SimulatedDevice::SimulatedDevice(common::Uuid id, device::Mode initial, Config cfg) noexcept
    : id_(id),
      mode_(initial),
      cfg_(cfg),
      load_(cfg.load_initial),
      temp_(cfg.temp_initial),
      rng_state_(cfg.seed != 0 ? cfg.seed : default_seed) {}

// xorshift64. [-1, 1) 균등.
double SimulatedDevice::next_noise() noexcept {
    std::uint64_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state_ = x;
    double const u = static_cast<double>(x >> 11) / static_cast<double>(1ULL << 53); // [0,1)
    return 2.0 * u - 1.0;
}

Status SimulatedDevice::query() {
    double const dt = cfg_.tick_seconds;

    load_ += load_rate(cfg_, mode_) * dt;
    if (cfg_.load_noise > 0.0) {
        load_ += next_noise() * cfg_.load_noise;
    }
    load_ = std::clamp(load_, 0.0, 100.0);

    temp_ += temp_rate(cfg_, mode_) * dt;
    if (cfg_.temp_noise > 0.0) {
        temp_ += next_noise() * cfg_.temp_noise;
    }
    temp_ = std::clamp(temp_, cfg_.temp_ambient, cfg_.temp_max);

    return Status{.mode = mode_, .load = load_, .temp = temp_};
}

bool SimulatedDevice::apply(device::Mode mode) {
    mode_ = mode;
    return true;
}

} // namespace ddcs::agent::domain
