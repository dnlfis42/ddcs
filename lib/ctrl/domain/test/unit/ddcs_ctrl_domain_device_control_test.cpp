#include "ddcs/ctrl/domain/device_control.hpp"

#include "ddcs/ctrl/domain/command_id.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"

#include <optional>

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::domain::CommandId;
using ddcs::ctrl::domain::DeviceControl;
using ddcs::ctrl::domain::DeviceThermalRegime;
using ddcs::ctrl::domain::GroupLoadRegime;
using ddcs::ctrl::domain::GroupRule;
using ddcs::ctrl::domain::ThermalRule;
using ddcs::device::Mode;

constexpr double cool_temp = 40.0;     // 해제 온도 70 아래
constexpr double deadband_temp = 80.0; // 해제 온도 70과 과열 온도 90 사이
constexpr double hot_temp = 95.0;      // 과열 온도 90 위

// 부하가 높으면 safe, 낮으면 normal을 사용한다.
GroupRule load_rule() {
    return GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal).value();
}

// load_rule에 온도 90에서 performance로 전환하고 70에서 해제하는 규칙을 더한다.
GroupRule thermal_rule() {
    return GroupRule::create(
               80.0, 20.0, Mode::safe, Mode::normal,
               ThermalRule{.hot_temp = 90.0, .cool_temp = 70.0, .hot_mode = Mode::performance}
    )
        .value();
}

// 부하 상태가 미정인 동안 과열되어 보호 모드를 발행하고 전송에 성공한 상태를 만든다.
void issue_protection(DeviceControl& control) {
    ASSERT_EQ(
        control.decide(thermal_rule(), GroupLoadRegime::unknown, hot_temp), Mode::performance
    );
    control.dispatched(CommandId{1});
}

TEST(DeviceControlTest, IssuesLoadModeWithoutRecord) {
    DeviceControl busy;
    EXPECT_EQ(busy.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);

    DeviceControl idle;
    EXPECT_EQ(idle.decide(load_rule(), GroupLoadRegime::idle, cool_temp), Mode::normal);
}

TEST(DeviceControlTest, SkipsUnchangedTargetWithoutFailure) {
    DeviceControl control;
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.dispatched(CommandId{1});

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), std::nullopt);
}

TEST(DeviceControlTest, IssuesChangedTarget) {
    DeviceControl control;
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.dispatched(CommandId{1});

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::idle, cool_temp), Mode::normal);
}

TEST(DeviceControlTest, ReissuesSameTargetAfterFinalFailure) {
    DeviceControl control;
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.dispatched(CommandId{1});
    control.failed(CommandId{1});

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
}

TEST(DeviceControlTest, ReissuesSameTargetAfterInitialSendFailure) {
    DeviceControl control;
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.dispatched(CommandId{}); // 첫 전송에 실패하면 무효 ID를 받는다.

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
}

TEST(DeviceControlTest, IgnoresFailureOfReplacedCommand) {
    DeviceControl control;
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.dispatched(CommandId{1});
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::idle, cool_temp), Mode::normal);
    control.dispatched(CommandId{2});

    control.failed(CommandId{1}); // 교체된 명령의 실패는 새 목표에 영향을 주지 않는다.

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::idle, cool_temp), std::nullopt);
}

TEST(DeviceControlTest, DefersWithoutBasisForTarget) {
    DeviceControl control;

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::unknown, cool_temp), std::nullopt);
}

TEST(DeviceControlTest, ProtectionModeWinsOverLoadMode) {
    DeviceControl control;

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::busy, hot_temp), Mode::performance);
    EXPECT_EQ(control.thermal(), DeviceThermalRegime::hot);
}

TEST(DeviceControlTest, KeepsProtectionModeInThermalDeadband) {
    DeviceControl control;
    ASSERT_EQ(control.decide(thermal_rule(), GroupLoadRegime::busy, hot_temp), Mode::performance);
    control.dispatched(CommandId{1});

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::busy, deadband_temp), std::nullopt);
    EXPECT_EQ(control.thermal(), DeviceThermalRegime::hot);
}

TEST(DeviceControlTest, ReleasesToLoadModeBelowCoolTemp) {
    DeviceControl control;
    ASSERT_EQ(control.decide(thermal_rule(), GroupLoadRegime::busy, hot_temp), Mode::performance);
    control.dispatched(CommandId{1});

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    EXPECT_EQ(control.thermal(), DeviceThermalRegime::cool);
}

// 부하 상태가 미정이어도 보호 모드를 명령했던 Device는 냉각되면 idle_mode로 복귀한다.
TEST(DeviceControlTest, ReleasesProtectionToIdleModeWhenRegimeUnknown) {
    DeviceControl control;
    ASSERT_EQ(
        control.decide(thermal_rule(), GroupLoadRegime::unknown, hot_temp), Mode::performance
    );
    control.dispatched(CommandId{1});

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), Mode::normal);
    control.dispatched(CommandId{2});
    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), std::nullopt);
}

TEST(DeviceControlTest, ReissuesFailedProtectionReleaseWhenRegimeUnknown) {
    DeviceControl control;
    ASSERT_EQ(
        control.decide(thermal_rule(), GroupLoadRegime::unknown, hot_temp), Mode::performance
    );
    control.dispatched(CommandId{1});
    ASSERT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), Mode::normal);
    control.dispatched(CommandId{2});
    control.failed(CommandId{2});

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), Mode::normal);
}

// 목표를 결정할 근거가 없어도 최종 실패한 명령의 목표는 다시 발행한다.
TEST(DeviceControlTest, ReissuesFailedTargetWithoutBasisForTarget) {
    DeviceControl control;
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.dispatched(CommandId{1});
    control.failed(CommandId{1});

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::unknown, cool_temp), Mode::safe);
}

TEST(DeviceControlTest, ReissuesSameTargetAfterPolicyReplacedOrReleased) {
    DeviceControl replaced;
    ASSERT_EQ(replaced.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    replaced.dispatched(CommandId{1});
    replaced.policy_replaced();
    EXPECT_EQ(replaced.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);

    DeviceControl released;
    ASSERT_EQ(released.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    released.dispatched(CommandId{1});
    released.released();
    EXPECT_EQ(released.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
}

// 온도 보호 규칙을 제거한 정책으로 바꿔도 보호 모드를 명령했던 Device는 idle_mode로 복귀한다.
TEST(DeviceControlTest, ReleasesProtectionAfterThermalRuleRemoved) {
    DeviceControl control;
    issue_protection(control);

    control.policy_replaced();

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::unknown, deadband_temp), Mode::normal);
}

// 같은 정책을 다시 적용한 뒤 냉각되어도 보호 모드를 명령했던 Device는 idle_mode로 복귀한다.
TEST(DeviceControlTest, ReleasesProtectionAfterPolicyReplaced) {
    DeviceControl control;
    issue_protection(control);

    control.policy_replaced();

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), Mode::normal);
}

// 연결이 끊겨도 과열 상태와 보호 모드 명령 여부는 유지한다.
TEST(DeviceControlTest, ReleasedKeepsThermalLatchAndProtection) {
    DeviceControl control;
    issue_protection(control);

    control.released();

    EXPECT_EQ(
        control.decide(thermal_rule(), GroupLoadRegime::unknown, deadband_temp), Mode::performance
    );
    EXPECT_EQ(control.thermal(), DeviceThermalRegime::hot);
    control.dispatched(CommandId{2});
    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), Mode::normal);
}

// 보호 해제 명령이 최종 실패한 뒤 정책을 다시 적용해도 idle_mode를 다시 발행한다.
TEST(DeviceControlTest, ReissuesFailedProtectionReleaseAfterPolicyReplaced) {
    DeviceControl control;
    issue_protection(control);
    ASSERT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), Mode::normal);
    control.dispatched(CommandId{2});
    control.failed(CommandId{2});

    control.policy_replaced();

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), Mode::normal);
}

// 부하 상태가 정해진 뒤 과열이 아닌 목표를 발행하면 보호 모드 해제 판단을 지운다.
TEST(DeviceControlTest, LoadModeIssueClearsProtection) {
    DeviceControl control;
    issue_protection(control);
    ASSERT_EQ(control.decide(thermal_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.dispatched(CommandId{2});

    control.policy_replaced();

    EXPECT_EQ(control.decide(thermal_rule(), GroupLoadRegime::unknown, cool_temp), std::nullopt);
}

// 전송 중 연결이 종료된 뒤 전송 결과를 받아도 재접속하면 같은 목표를 다시 발행한다.
TEST(DeviceControlTest, ReleaseDuringDispatchReissuesAfterReconnect) {
    DeviceControl control;
    ASSERT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
    control.released();
    control.dispatched(CommandId{1});

    EXPECT_EQ(control.decide(load_rule(), GroupLoadRegime::busy, cool_temp), Mode::safe);
}

} // namespace
