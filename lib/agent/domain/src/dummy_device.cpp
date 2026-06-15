#include "ddcs/agent/domain/dummy_device.hpp"

namespace ddcs::agent::domain {

DummyDevice::DummyDevice(device::Mode initial) noexcept : mode_{initial} {}

DeviceState DummyDevice::query() { return DeviceState{.mode = mode_, .load = load_, .temp = temp_}; }

bool DummyDevice::apply(device::SetMode const& cmd) {
    mode_ = cmd.mode; // 유일 명령 SetMode라서 항상 성공
    return true;
}

void DummyDevice::set_mode(device::Mode mode) noexcept { mode_ = mode; }

} // namespace ddcs::agent::domain
