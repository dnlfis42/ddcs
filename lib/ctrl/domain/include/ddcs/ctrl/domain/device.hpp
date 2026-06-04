#pragma once

#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/device/status.hpp"

#include <string>

namespace ddcs::ctrl::domain {

// Device 트윈 레코드(controller 가 가진 디바이스 모델). DeviceRegistry가 소유.
// binding(현재 어느 connection에 연결돼 있는지)은 app::session 책임 - 여기엔 없음.
// id/uuid = 영속 identity. group/version = 등록 시 선언. status = 최근 관측 상태(mode/load/temp).
struct Device {
    DeviceId id;
    common::Uuid uuid;
    std::string group{};        // 논리 그룹(정책 타깃팅). 빈 문자열 = 미지정
    std::string version{};      // device(agent) 앱/빌드 버전
    device::Status status{};    // 최근 관측 상태(기본 safe/0)
};

} // namespace ddcs::ctrl::domain
