#pragma once

#include "ddcs/ctrl/app/device/port/active_devices.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace ddcs::ctrl::app::device {

// group별 active Device 집계값. 정책 평가는 load_sum/device_count만 읽고, 메트릭 노출은 전부
// 읽는다.
struct GroupAggregate {
    std::uint64_t device_count{};
    double load_sum{};
    double temp_sum{};
    std::array<std::uint64_t, 3> by_mode{}; // index = encode_mode (safe/normal/performance)
};

// 정책 평가와 메트릭 노출이 공유하는 집계 규칙의 단일 소유자:
// active 집합만, 관측(Status)이 있는 Shadow와 정책이 아는 Group만 센다.
// 반환은 label 출력 순서 안정을 위해 ordered map이다.
[[nodiscard]] std::map<std::string, GroupAggregate> aggregate_groups(
    port::ActiveDevices& active_devices, domain::DeviceRegistry const& devices,
    domain::GroupPolicy const& policy
);

} // namespace ddcs::ctrl::app::device
