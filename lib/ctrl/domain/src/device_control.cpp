#include "ddcs/ctrl/domain/device_control.hpp"

#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"

#include <optional>

namespace ddcs::ctrl::domain {

std::optional<device::Mode>
DeviceControl::decide(GroupRule const& rule, GroupLoadRegime regime, double temp) noexcept {
    thermal_ = rule.next_thermal(thermal_, temp);

    std::optional<device::Mode> target;
    if (thermal_ == DeviceThermalRegime::hot && rule.thermal()) {
        target = rule.thermal()->hot_mode; // 과열 상태는 부하 상태보다 우선한다.
    } else {
        switch (regime) {
        case GroupLoadRegime::idle:
            target = rule.idle_mode();
            break;
        case GroupLoadRegime::busy:
            target = rule.busy_mode();
            break;
        case GroupLoadRegime::unknown:
            if (protection_commanded_) {
                target =
                    rule.idle_mode(); // 정책에서 온도 보호 규칙이 빠져도 같은 기준으로 해제한다.
            } else if (failed_) {
                target = mode_; // 최종 실패한 명령의 목표를 다시 사용한다.
            }
            break;
        }
    }
    if (!target) {
        return std::nullopt;
    }
    if (mode_ == target && !failed_) {
        return std::nullopt; // 목표가 같고 실패하지 않은 명령은 다시 발행하지 않는다.
    }
    mode_ = target;
    failed_ = false;
    awaiting_dispatch_ = true;
    if (thermal_ == DeviceThermalRegime::hot) {
        protection_commanded_ = true;
    } else if (regime != GroupLoadRegime::unknown) {
        protection_commanded_ = false; // 부하 상태는 다시 미정으로 돌아가지 않는다.
    }
    return target;
}

void DeviceControl::dispatched(CommandId command) noexcept {
    if (!awaiting_dispatch_) {
        return;
    }
    awaiting_dispatch_ = false;
    command_ = command;
    failed_ = !command.valid();
}

void DeviceControl::failed(CommandId command) noexcept {
    if (command == command_) {
        failed_ = true;
    }
}

void DeviceControl::clear_record() noexcept {
    mode_.reset();
    command_.clear();
    failed_ = false;
    awaiting_dispatch_ = false;
}

} // namespace ddcs::ctrl::domain
