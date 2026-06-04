#pragma once

#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/domain/device.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <string>
#include <unordered_map>

#include <cstddef>
#include <cstdint>

namespace ddcs::ctrl::domain {

// Device 식별의 영속 저장소: uuid <-> id 매핑.
// 동일 uuid의 재등록 시 동일 DeviceId가 보장된다(identity persistence across reconnects).
// 현재 connection과의 binding은 app::session이 별도로 관리한다.
class DeviceRegistry {
public:
    DeviceRegistry() = default;
    ~DeviceRegistry() = default;

    DeviceRegistry(DeviceRegistry const&) = delete;
    DeviceRegistry& operator=(DeviceRegistry const&) = delete;
    DeviceRegistry(DeviceRegistry&&) noexcept = delete;
    DeviceRegistry& operator=(DeviceRegistry&&) noexcept = delete;

    // 주어진 uuid에 대응하는 Device를 반환. 없으면 새로 생성(다음 DeviceId 할당).
    // unordered_map 요소는 rehash에도 안정적이므로 const& 반환 안전.
    Device const& find_or_create(common::Uuid const& uuid);

    // 등록 시 선언된 가변 속성(group/version) 갱신. identity(id/uuid)는 불변. 미지의 id는 무시.
    void set_attributes(DeviceId id, std::string group, std::string version);

    // 최근 Status 텔레메트리(mode/load/temp) 반영. 미지의 id는 무시.
    void update_telemetry(DeviceId id, device::Mode mode, double load, double temp);

    // 조회 (없으면 nullptr).
    Device const* find_by_uuid(common::Uuid const& uuid) const;
    Device const* find_by_id(DeviceId id) const;

    std::size_t size() const noexcept { return by_uuid_.size(); }

private:
    std::uint64_t next_device_id_{0};
    std::unordered_map<common::Uuid, Device> by_uuid_;
    std::unordered_map<DeviceId, common::Uuid> id_to_uuid_; // 역인덱스 (id 조회)
};

} // namespace ddcs::ctrl::domain
