#pragma once

#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/device/mode.hpp"

#include <string>

namespace ddcs::ctrl::domain {

// Device 트윈 레코드(controller 가 가진 디바이스 모델). DeviceRegistry가 소유.
// binding(현재 어느 connection에 연결돼 있는지)은 app::session 책임 - 여기엔 없음.
// id/uuid = 영속 identity. group/version = 등록 시 선언. mode/load/temp = 최근 Status 텔레메트리.
struct Device {
    DeviceId id;
    common::Uuid uuid;
    std::string group{};   // 논리 그룹(정책 타깃팅). 빈 문자열 = 미지정
    std::string version{}; // device(agent) 앱/빌드 버전
    device::Mode mode{};   // 현재 모드(기본 safe). 정책 출력+피드백
    double load{};         // 부하 가상지표 0~100. 정책 입력 신호
    double temp{};         // 온도 C
};

} // namespace ddcs::ctrl::domain
