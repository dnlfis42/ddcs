#include "ddcs/agent/domain/simulated_device.hpp"

#include <cmath>

namespace ddcs::agent::domain {

SimulatedDevice::SimulatedDevice(device::Mode initial) noexcept : SimulatedDevice{initial, Config{}} {}

SimulatedDevice::SimulatedDevice(device::Mode initial, Config cfg) noexcept : mode_{initial}, cfg_{cfg} {}

DeviceState SimulatedDevice::query() {
    phase_ += cfg_.step;
    double const load = cfg_.baseline + cfg_.amplitude * std::sin(phase_);
    return DeviceState{.mode = mode_, .load = load, .temp = cfg_.temp};
}

bool SimulatedDevice::apply(proto::cmd::SetMode const& cmd) {
    mode_ = cmd.mode;
    return true;
}

} // namespace ddcs::agent::domain
