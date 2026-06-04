#include "ddcs/ctrl/domain/device_registry.hpp"

#include <utility>

namespace ddcs::ctrl::domain {

Device const& DeviceRegistry::find_or_create(common::Uuid const& uuid) {
    auto it = by_uuid_.find(uuid);
    if (it != by_uuid_.end()) {
        return it->second;
    }
    DeviceId const new_id{++next_device_id_};
    // group/version 은 빈 문자열로 시작 - 등록 시 set_attributes 가 채운다.
    auto [ins_it, inserted] = by_uuid_.emplace(uuid, Device{.id = new_id, .uuid = uuid});
    id_to_uuid_.emplace(new_id, uuid);
    return ins_it->second;
}

void DeviceRegistry::set_attributes(DeviceId id, std::string group, std::string version) {
    auto const idx = id_to_uuid_.find(id);
    if (idx == id_to_uuid_.end()) {
        return; // 미지의 id - 방어적 무시
    }
    auto& device = by_uuid_.at(idx->second);
    device.group = std::move(group);
    device.version = std::move(version);
}

void DeviceRegistry::update_status(DeviceId id, device::Status status) {
    auto const idx = id_to_uuid_.find(id);
    if (idx == id_to_uuid_.end()) {
        return; // 미지의 id - 방어적 무시
    }
    by_uuid_.at(idx->second).status = status;
}

Device const* DeviceRegistry::find_by_uuid(common::Uuid const& uuid) const {
    auto const it = by_uuid_.find(uuid);
    return it == by_uuid_.end() ? nullptr : &it->second;
}

Device const* DeviceRegistry::find_by_id(DeviceId id) const {
    auto const idx = id_to_uuid_.find(id);
    if (idx == id_to_uuid_.end()) {
        return nullptr;
    }
    auto const it = by_uuid_.find(idx->second);
    return it == by_uuid_.end() ? nullptr : &it->second;
}

} // namespace ddcs::ctrl::domain
