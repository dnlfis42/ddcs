#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"

#include <functional>

namespace ddcs::ctrl::app::device::port {

// 명령 가능한(등록 확인된 active) device 열거 포트
// - 연결 상태의 단일 진실은 구현이 가진다.
class DeviceRoster {
public:
    virtual ~DeviceRoster() = default;

    // CAUTION: 콜백 안에서 연결 상태를 바꾸는 호출을 하지 말 것 (구현이 등록부 순회 중)
    virtual void for_each_active(std::function<void(domain::DeviceId)> const& fn) = 0;
};

} // namespace ddcs::ctrl::app::device::port
