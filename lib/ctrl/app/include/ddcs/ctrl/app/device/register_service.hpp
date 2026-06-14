#pragma once

#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"

#include <string_view>

namespace ddcs::ctrl::app::device {

// 등록 identity 확정 use-case. uuid를 Device 트윈으로 해소하고 선언된 group을 반영한다.
class RegisterService {
public:
    explicit RegisterService(domain::DeviceRegistry& devices) noexcept : devices_{devices} {}

    // uuid -> DeviceId 확정 (없으면 Device 생성) + group 갱신. nil uuid는 무효 DeviceId로 거부.
    [[nodiscard]] domain::DeviceId enroll(common::Uuid const& id, std::string_view group);

private:
    domain::DeviceRegistry& devices_;
};

} // namespace ddcs::ctrl::app::device
