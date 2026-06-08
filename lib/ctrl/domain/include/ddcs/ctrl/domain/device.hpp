#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/device/status.hpp"

#include <string>

namespace ddcs::ctrl::domain {

// Device 트윈 레코드(controller가 가진 디바이스 모델). DeviceRegistry가 소유.
struct Device {
    DeviceId id;
    std::string group{};     // 논리 그룹(정책 타깃팅). 빈 문자열 = 미지정
    device::Status status{}; // 최근 관측 상태(기본 safe/0)
};

} // namespace ddcs::ctrl::domain
