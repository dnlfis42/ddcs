#include "ddcs/agent/domain/dummy_device.hpp"

namespace ddcs::agent::domain {

DummyDevice::DummyDevice(common::Uuid id, device::Mode initial) noexcept
    : id_(id),
      mode_(initial) {}

Status DummyDevice::query() {
    return Status{.mode = mode_, .load = load_, .temp = temp_};
}

bool DummyDevice::apply(device::Mode mode) {
    mode_ = mode; // mode 설정은 항상 성공
    return true;
}

void DummyDevice::set_mode(device::Mode mode) noexcept {
    mode_ = mode;
}

} // namespace ddcs::agent::domain
