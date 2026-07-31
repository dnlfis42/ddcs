#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/active_devices.hpp"
#include "ddcs/ctrl/app/device/port/device_release_sink.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/json/value.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ddcs::ctrl::app::device {

// policy JSON(groups 객체)을 GroupPolicy로 변환. 필드 누락/타입오류/미지 mode면 nullopt
std::optional<domain::GroupPolicy> parse_policy(json::Value const& root);

// 정책 엔진. load와 thermal을 합성한 effective mode가 바뀐 device에만 SetMode를 발행한다.
class PolicyService : public port::DeviceReleaseSink {
public:
    PolicyService(
        port::ActiveDevices& active_devices, domain::DeviceRegistry& devices,
        CommandService& commands
    ) noexcept
        : active_devices_{active_devices},
          devices_{devices},
          commands_{commands} {}

    // load/핫리로드 apply: commanded만 비움, latch 보존
    void set_policy(domain::GroupPolicy policy);

    // 주기 호출(조립 루트 tick)
    void evaluate(common::Clock::time_point now);

    // DeviceReleaseSink: device 세션 종료 시 그 device의 제어 belief(commanded/thermal) 폐기
    void on_device_released(domain::DeviceId device) override;

    // 로드된 정책 (알려진 그룹 집합)
    // - set_policy가 같은 멤버를 in-place 교체해도 참조는 계속 유효하다.
    [[nodiscard]] domain::GroupPolicy const& policy() const noexcept {
        return policy_;
    }

private:
    void
    command_one(domain::DeviceId device, ddcs::device::Mode mode, common::Clock::time_point now);

    port::ActiveDevices& active_devices_;
    domain::DeviceRegistry& devices_;
    CommandService& commands_;
    domain::GroupPolicy policy_;
    std::unordered_map<std::string, domain::Regime>
        regime_; // group의 load regime (load는 그룹 단위)
    std::unordered_map<domain::DeviceId, domain::Thermal>
        thermal_; // device별 thermal 상태 (per-device 트립)
    // device별 마지막 발신 effective mode (group base + 자기 thermal). 변할 때만 발신.
    std::unordered_map<domain::DeviceId, std::optional<ddcs::device::Mode>> commanded_;
    std::vector<std::pair<domain::DeviceId, ddcs::device::Mode>> pending_; // 순회 밖 발송 버퍼
};

} // namespace ddcs::ctrl::app::device
