#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/device/status.hpp"

#include <optional>
#include <string>

namespace ddcs::ctrl::domain {

// DeviceShadow 레코드 (controller가 가진 Device 모델)
// - DeviceRegistry가 소유한다.
struct DeviceShadow {
    DeviceId id;
    std::string group{};                    // 논리 Group(정책 타깃팅). 빈 문자열 = 미지정
    std::optional<device::Status> status{}; // 최근 관측 상태. nullopt = 아직 관측 없음
};

} // namespace ddcs::ctrl::domain
