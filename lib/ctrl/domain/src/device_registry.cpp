#include "ddcs/ctrl/domain/device_registry.hpp"

#include <utility>

namespace ddcs::ctrl::domain {

Device const& DeviceRegistry::find_or_create(DeviceId id) {
    // id(uuid)가 곧 키이며 서로게이트 발급 없음. 동일 uuid 재등록 시 기존 Device 반환.
    return devices_.try_emplace(id, Device{.id = id}).first->second;
}

void DeviceRegistry::set_group(DeviceId id, std::string group) {
    auto const it = devices_.find(id);
    if (it == devices_.end()) {
        return; // 미지의 id, 방어적 무시
    }
    it->second.group = std::move(group);
}

void DeviceRegistry::update_status(DeviceId id, device::Status status) {
    auto const it = devices_.find(id);
    if (it == devices_.end()) {
        return; // 미지의 id, 방어적 무시
    }
    it->second.status = status;
}

Device const* DeviceRegistry::find(DeviceId id) const {
    auto const it = devices_.find(id);
    return it == devices_.end() ? nullptr : &it->second;
}

} // namespace ddcs::ctrl::domain
