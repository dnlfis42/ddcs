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

// 정책 JSON을 GroupPolicy로 변환한다. 필수 필드, 타입, 모드가 유효하지 않으면 nullopt를 반환한다.
std::optional<domain::GroupPolicy> parse_policy(json::Value const& root);

// Group 부하와 Device 온도로 목표 모드를 결정한다. 목표가 바뀌거나 이전 명령이 실패하면 SetMode를 발행한다.
class PolicyService : public port::DeviceReleaseSink, public port::CommandFailureSink {
public:
    PolicyService(
        port::ActiveDevices& active_devices, domain::DeviceRegistry& devices,
        CommandService& commands
    ) noexcept
        : active_devices_{active_devices},
          devices_{devices},
          commands_{commands} {}

    ~PolicyService() override;
    PolicyService(PolicyService const&) = delete;
    PolicyService& operator=(PolicyService const&) = delete;
    PolicyService(PolicyService&&) = delete;
    PolicyService& operator=(PolicyService&&) = delete;

    // 정책을 적용하고 명령 기록을 비운다. 기존 부하 상태와 과열 상태는 유지한다.
    void set_policy(domain::GroupPolicy policy);

    // Controller의 tick마다 정책을 평가한다.
    void evaluate(common::Clock::time_point now);

    // Device 연결이 종료되면 해당 Device의 명령 기록과 과열 상태를 제거한다.
    void on_device_released(domain::DeviceId device) override;

    // 현재 명령의 최종 실패를 기록한다. 재발행 여부는 다음 evaluate()에서 결정한다.
    void on_command_failed(domain::DeviceId device, port::CommandId command) noexcept override;

    // 현재 적용된 정책
    // set_policy()로 내용을 교체해도 정책 객체에 대한 참조는 유효하다.
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
    std::unordered_map<std::string, domain::GroupLoadRegime>
        regime_; // Group별 부하 상태
    std::unordered_map<domain::DeviceId, domain::DeviceThermalRegime>
        thermal_; // Device별 과열 상태
    struct CommandBelief {
        std::optional<ddcs::device::Mode> mode;
        port::CommandId command;
        bool failed = false;
    };
    // 과열 해제 후 복귀할 목표를 유지한다. 명령 실패 시에도 목표 모드를 보존해 다시 발행한다.
    std::unordered_map<domain::DeviceId, CommandBelief> commanded_;
    std::vector<std::pair<domain::DeviceId, ddcs::device::Mode>> pending_; // Device 순회를 마친 뒤 전송할 명령 목록
};

} // namespace ddcs::ctrl::app::device
