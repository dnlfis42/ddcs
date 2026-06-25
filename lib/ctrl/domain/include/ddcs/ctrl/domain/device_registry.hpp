#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_shadow.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace ddcs::ctrl::domain {

// DeviceShadow의 영속 저장소 (uuid = DeviceId 키)
// - 동일 uuid 재등록 시 동일 DeviceShadow
// - connection과의 binding은 app::session::SessionRegistry가 별도로 관리한다.
class DeviceRegistry {
public:
    DeviceRegistry() = default;
    ~DeviceRegistry() = default;

    DeviceRegistry(DeviceRegistry const&) = delete;
    DeviceRegistry& operator=(DeviceRegistry const&) = delete;
    DeviceRegistry(DeviceRegistry&&) noexcept = delete;
    DeviceRegistry& operator=(DeviceRegistry&&) noexcept = delete;

    // 주어진 id(uuid)의 Device를 반환. 없으면 새로 생성
    // unordered_map 요소는 rehash에도 안정적이므로 const& 반환 안전
    DeviceShadow const& find_or_create(DeviceId id);

    // 등록 시 선언된 group 갱신. 미지의 id는 무시
    void set_group(DeviceId id, std::string group);

    // 최근 관측 상태(Status) 반영. 미지의 id는 무시
    void update_status(DeviceId id, Status status);

    // 조회 (없으면 nullptr)
    DeviceShadow const* find(DeviceId id) const;

    std::size_t size() const noexcept {
        return devices_.size();
    }

private:
    std::unordered_map<DeviceId, DeviceShadow> devices_;
};

} // namespace ddcs::ctrl::domain
