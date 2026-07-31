#include "ddcs/ctrl/domain/device_registry.hpp"

#include <utility>

namespace ddcs::ctrl::domain {

void DeviceRegistry::enroll(DeviceId id, std::string group) {
    // id(uuid)가 곧 키이며 서로게이트 발급 없음. 재등록이면 기존 Shadow(직전 관측)를 보존한다.
    auto& shadow = devices_.try_emplace(id, DeviceShadow{.id = id}).first->second;
    shadow.group = std::move(group);
}

bool DeviceRegistry::update_status(DeviceId id, device::Status status) {
    auto const it = devices_.find(id);
    if (it == devices_.end()) {
        return false; // 미지의 id: 등록 없이 온 보고
    }
    it->second.status = status;
    return true;
}

DeviceShadow const* DeviceRegistry::find(DeviceId id) const {
    auto const it = devices_.find(id);
    return it == devices_.end() ? nullptr : &it->second;
}

} // namespace ddcs::ctrl::domain
