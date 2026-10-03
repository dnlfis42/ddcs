#pragma once

#include "ddcs/ctrl/domain/command_id.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"

#include <optional>

namespace ddcs::ctrl::domain {

// 정책이 Device 하나에 대해 기억하는 판단 상태다. 과열 상태, 발행 기록, 보호 모드를 마지막으로
// 명령했는지를 함께 담으며, DeviceShadow처럼 연결이 끊겨도 남는다.
class DeviceControl {
public:
    // 이번 평가에서 발행할 동작 모드를 결정한다. 생략하거나 보류하면 nullopt다.
    [[nodiscard]] std::optional<device::Mode>
    decide(GroupRule const& rule, GroupLoadRegime regime, double temp) noexcept;

    // decide()가 반환한 명령의 첫 전송 결과를 기록한다. 무효 ID면 실패로 기록한다.
    // 전송 대기 중이 아니면(전송 중 연결 종료나 정책 교체로 취소됨) 무시한다.
    void dispatched(CommandId command) noexcept;

    // 명령의 최종 실패를 기록한다. 마지막으로 발행한 명령과 같을 때만 반영한다.
    void failed(CommandId command) noexcept;

    // 정책 교체: 발행 기록을 비운다. 과열 상태와 보호 모드 명령 여부는 유지한다.
    void policy_replaced() noexcept {
        clear_record();
    }

    // 연결 종료: 발행 기록을 비운다. 과열 상태와 보호 모드 명령 여부는 유지한다.
    void released() noexcept {
        clear_record();
    }

    // 마지막 decide()에서 판단한 과열 상태
    [[nodiscard]] DeviceThermalRegime thermal() const noexcept {
        return thermal_;
    }

private:
    void clear_record() noexcept;

    DeviceThermalRegime thermal_ = DeviceThermalRegime::cool;
    // 보호 모드를 마지막으로 명령했는지. 부하 상태가 미정일 때 냉각되면 idle_mode로 복귀시킨다.
    // 부하 상태가 정해진 뒤 과열이 아닌 목표를 발행할 때 지운다.
    bool protection_commanded_ = false;
    // 발행 기록: 마지막으로 발행한 목표 동작 모드와 그 명령의 실패 여부
    std::optional<device::Mode> mode_;
    CommandId command_;
    bool failed_ = false;
    bool awaiting_dispatch_ = false; // decide()가 발행을 반환한 뒤 dispatched()를 기다리는 중
};

} // namespace ddcs::ctrl::domain
