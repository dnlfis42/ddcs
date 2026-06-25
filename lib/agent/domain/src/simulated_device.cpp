#include "ddcs/agent/domain/simulated_device.hpp"

#include <cmath>

namespace ddcs::agent::domain {

SimulatedDevice::SimulatedDevice(common::Uuid id, device::Mode initial) noexcept
    : SimulatedDevice(id, initial, Config{}) {}

SimulatedDevice::SimulatedDevice(common::Uuid id, device::Mode initial, Config cfg) noexcept
    : id_(id),
      mode_(initial),
      cfg_(cfg) {}

Status SimulatedDevice::query() {
    phase_ += cfg_.step;
    double const load = cfg_.baseline + cfg_.amplitude * std::sin(phase_);
    return Status{.mode = mode_, .load = load, .temp = cfg_.temp};
}

bool SimulatedDevice::apply(device::Mode mode) {
    mode_ = mode;
    return true;
}

} // namespace ddcs::agent::domain
