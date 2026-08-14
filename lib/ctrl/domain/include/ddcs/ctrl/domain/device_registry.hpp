#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_shadow.hpp"
#include "ddcs/device/status.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>

namespace ddcs::ctrl::domain {

// DeviceShadow 저장소 (uuid = DeviceId 키). Shadow는 재접속을 넘어 유지된다.
// connection과의 binding은 app::session::SessionRegistry가 별도로 관리한다.
class DeviceRegistry {
public:
    DeviceRegistry() = default;
    ~DeviceRegistry() = default;

    DeviceRegistry(DeviceRegistry const&) = delete;
    DeviceRegistry& operator=(DeviceRegistry const&) = delete;
    DeviceRegistry(DeviceRegistry&&) noexcept = delete;
    DeviceRegistry& operator=(DeviceRegistry&&) noexcept = delete;

    // 등록: 없으면 Shadow를 만들고, 있으면 보존(직전 관측 유지)하되 선언된 group은 갱신한다
    void enroll(DeviceId id, std::string group);

    // 최근 관측 상태(Status) 반영. 미지의 id면 false (등록 없이 온 보고는 버그 신호)
    [[nodiscard]] bool update_status(DeviceId id, device::Status status);

    // 조회 (없으면 nullptr)
    [[nodiscard]] DeviceShadow const* find(DeviceId id) const;

    [[nodiscard]] std::size_t size() const noexcept {
        return devices_.size();
    }

private:
    std::unordered_map<DeviceId, DeviceShadow> devices_;
};

} // namespace ddcs::ctrl::domain
