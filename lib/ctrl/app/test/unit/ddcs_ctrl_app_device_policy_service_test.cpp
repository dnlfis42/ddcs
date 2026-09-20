#include "ddcs/ctrl/app/device/policy_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/active_devices.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/wire/command/command.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace json = ddcs::json;

using ddcs::common::ManualClock;
using ddcs::ctrl::app::device::CommandService;
using ddcs::ctrl::app::device::parse_policy;
using ddcs::ctrl::app::device::PolicyService;
using ddcs::ctrl::app::device::port::ActiveDevices;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::CommandSender;
using ddcs::ctrl::app::device::port::SendResult;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::domain::GroupPolicy;
using ddcs::ctrl::domain::GroupRule;
using ddcs::ctrl::domain::ThermalRule;
using ddcs::device::Mode;
using ddcs::device::Status;
using ddcs::wire::command::Command;
using ddcs::wire::command::SetMode;
using namespace std::chrono_literals;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

// 미리 지정한 활성 Device 목록을 제공한다.
class FakeActiveDevices final : public ActiveDevices {
public:
    std::vector<DeviceId> active;

    void for_each_active(std::function<void(DeviceId)> const& fn) override {
        for (auto const id : active) {
            fn(id);
        }
    }
};

// 요청받은 명령 전송을 기록한다.
class FakeCommandSender final : public CommandSender {
public:
    struct Sent {
        DeviceId device;
        CommandId command_id;
        Mode mode; // SetMode 명령 값
    };

    std::vector<Sent> sent;
    SendResult result = SendResult::ok;
    std::function<void()> on_send;

    SendResult send(DeviceId device, CommandId command_id, Command const& command) override {
        sent.push_back(
            Sent{
                .device = device,
                .command_id = command_id,
                .mode =
                    ddcs::device::decode_mode(std::get<SetMode>(command).mode).value_or(Mode::safe),
            }
        );
        if (on_send) {
            on_send();
        }
        return result;
    }
};

// 활성 Device 순회 중인지 표시해 전송 콜백의 재진입을 검증한다.
// 목록 복사본을 순회해 변경으로 인한 무효 참조를 피하고, 전송 대역에서 순회 중 송신 여부를 확인한다.
class WindowedActiveDevices final : public ActiveDevices {
public:
    std::vector<DeviceId> active;
    bool iterating = false;

    void for_each_active(std::function<void(DeviceId)> const& fn) override {
        iterating = true;
        auto const snapshot = active;
        for (auto const id : snapshot) {
            fn(id);
        }
        iterating = false;
    }
};

// 전송 시점에 활성 Device 목록을 순회 중이었는지 기록한다.
// 순회가 끝난 뒤 전송한다면 iterating은 false다.
class IterationProbeCommandSender final : public CommandSender {
public:
    explicit IterationProbeCommandSender(WindowedActiveDevices& active_devices) noexcept
        : active_devices_{active_devices} {}

    int sent_count = 0;
    bool dispatched_during_iteration = false;

    SendResult send(DeviceId, CommandId, Command const&) override {
        if (active_devices_.iterating) {
            dispatched_during_iteration = true;
        }
        ++sent_count;
        return SendResult::ok;
    }

private:
    WindowedActiveDevices& active_devices_;
};

struct PolicyFixture {
    ManualClock clock;
    FakeActiveDevices active_devices;
    DeviceRegistry devices;
    FakeCommandSender sender;
    CommandService commands;

    explicit PolicyFixture(int max_attempts = 1)
        : commands(sender, 5s, max_attempts, 500ms) {}
    PolicyService policy{active_devices, devices, commands};

    DeviceId enroll(std::uint8_t seed, std::string group, double load, bool active = true) {
        DeviceId const id = make_device_id(seed);
        devices.enroll(id, std::move(group));
        EXPECT_TRUE(
            devices.update_status(id, Status{.mode = Mode::normal, .load = load, .temp = 40.0})
        );
        if (active) {
            active_devices.active.push_back(id);
        }
        return id;
    }

    void set_load(DeviceId id, double load) {
        EXPECT_TRUE(
            devices.update_status(id, Status{.mode = Mode::normal, .load = load, .temp = 40.0})
        );
    }

    static GroupPolicy sensors_policy() {
        GroupPolicy p;
        p.set("sensors", GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal).value());
        return p;
    }

    // 부하가 높으면 safe, 낮으면 normal을 사용한다. 온도 90에서 performance로 전환하고 70에서 해제한다.
    static GroupPolicy hot_policy() {
        GroupPolicy p;
        p.set(
            "sensors",
            GroupRule::create(
                80.0, 20.0, Mode::safe, Mode::normal,
                ThermalRule{.hot_temp = 90.0, .cool_temp = 70.0, .hot_mode = Mode::performance}
            )
                .value()
        );
        return p;
    }
};

TEST(PolicyServiceTest, EvaluateWithoutPolicyDoesNothing) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 95.0);

    f.policy.evaluate(f.clock.now());

    EXPECT_TRUE(f.sender.sent.empty());
}

TEST(PolicyServiceTest, TransitionsToBusyAboveHighLoad) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 90.0);
    f.enroll(0x02, "sensors", 95.0);
    f.enroll(0x03, "pumps", 99.0); // 정책 없는 Group은 무관
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u); // sensors 멤버 전원, pumps 제외
    for (auto const& s : f.sender.sent) {
        EXPECT_EQ(s.mode, Mode::safe); // SetMode 명령의 모드 값을 확인한다.
    }
    EXPECT_EQ(f.commands.pending_count(), 2u); // CommandService에서 명령을 추적한다.
}

TEST(PolicyServiceTest, DefersUnobservedDeviceUntilFirstStatus) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 95.0);
    // 등록 후 아직 상태를 보고하지 않은 Device는 평균 집계와 명령 대상에서 제외한다.
    DeviceId const fresh = make_device_id(0x02);
    f.devices.enroll(fresh, "sensors");
    f.active_devices.active.push_back(fresh);
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 1u); // 보고된 부하 95만 집계해 busy로 판단한다.
    EXPECT_EQ(f.sender.sent[0].device, make_device_id(0x01));
    EXPECT_EQ(f.sender.sent[0].mode, Mode::safe);

    // 첫 보고부터 제어에 편입된다
    f.set_load(fresh, 90.0);
    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[1].device, fresh);
    EXPECT_EQ(f.sender.sent[1].mode, Mode::safe);
}

TEST(PolicyServiceTest, DoesNotRespamWhileRegimeUnchanged) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());
    f.policy.evaluate(f.clock.now()); // 같은 부하 상태에서 다시 평가한다.

    EXPECT_EQ(f.sender.sent.size(), 1u); // 전환마다 1회
}

TEST(PolicyServiceTest, ReissuesUnchangedTargetAfterInitialDispatchFailure) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.sender.result = SendResult::offline;
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.commands.pending_count(), 0u);
    ASSERT_EQ(f.commands.metrics().dispatch_failures_offline_total, 1u);

    f.sender.result = SendResult::ok;
    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent.back().mode, Mode::safe);
    EXPECT_EQ(f.commands.pending_count(), 1u);
}

TEST(PolicyServiceTest, ReissuesUnchangedTargetAfterRetryBudgetExhausted) {
    PolicyFixture f;
    auto const device = f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 1u);
    auto const first = f.sender.sent.front().command_id;
    f.clock.advance(6s);
    f.commands.sweep(f.clock.now());
    ASSERT_EQ(f.commands.pending_count(), 0u);
    ASSERT_EQ(f.commands.metrics().failed_exhausted_total, 1u);

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent.back().device, device);
    EXPECT_EQ(f.sender.sent.back().mode, Mode::safe);
    EXPECT_NE(f.sender.sent.back().command_id, first);
    EXPECT_EQ(f.commands.pending_count(), 1u);
}

TEST(PolicyServiceTest, DoesNotReissuePendingRetryOrSuccessfulCommand) {
    PolicyFixture f{2};
    auto const device = f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    auto const first = f.sender.sent.front().command_id;
    f.clock.advance(6s);
    f.commands.sweep(f.clock.now()); // 기존 명령의 재전송을 기다리는 중이다.
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 1u);
    f.clock.advance(1s);
    f.commands.sweep(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent.back().command_id, first);
    f.commands.settle(device, first, true, 0, f.clock.now());
    f.policy.evaluate(f.clock.now()); // 이전 상태 보고의 모드만으로 명령 실패를 판단하지 않는다.
    EXPECT_EQ(f.sender.sent.size(), 2u);
}

TEST(PolicyServiceTest, ReissuesAfterAgentFailureExhaustsBudget) {
    PolicyFixture f;
    auto const device = f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    auto const first = f.sender.sent.front().command_id;
    f.commands.settle(device, first, false, 1, f.clock.now());
    EXPECT_EQ(f.sender.sent.size(), 1u); // 실패 콜백에서는 즉시 전송하지 않는다.
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_NE(f.sender.sent.back().command_id, first);
}

TEST(PolicyServiceTest, ReissuesAfterRetrySendFailure) {
    PolicyFixture f{2};
    f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    f.clock.advance(6s);
    f.commands.sweep(f.clock.now());
    f.sender.result = SendResult::encode_fail;
    f.clock.advance(1s);
    f.commands.sweep(f.clock.now());
    ASSERT_EQ(f.commands.metrics().failed_encode_fail_total, 1u);
    ASSERT_EQ(f.commands.pending_count(), 0u);
    f.sender.result = SendResult::ok;
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 3u);
    EXPECT_EQ(f.commands.pending_count(), 1u);
}

TEST(PolicyServiceTest, OldFailureDoesNotInvalidateNewTarget) {
    PolicyFixture f;
    auto const device = f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    auto const old = f.sender.sent.front().command_id;
    f.set_load(device, 10.0);
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 2u);
    f.commands.settle(device, old, false, 1, f.clock.now());
    f.policy.evaluate(f.clock.now());
    EXPECT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent.back().mode, Mode::normal);
}

TEST(PolicyServiceTest, ReissuesFailedThermalRecoveryWhenGroupRegimeUnknown) {
    PolicyFixture f;
    auto const device = f.enroll(0x01, "sensors", 50.0);
    f.policy.set_policy(PolicyFixture::hot_policy());
    ASSERT_TRUE(
        f.devices.update_status(device, Status{.mode = Mode::normal, .load = 50.0, .temp = 95.0})
    );
    f.policy.evaluate(f.clock.now()); // 과열 상태이며 부하는 두 임계값 사이에 있다.
    auto const hot = f.sender.sent.back().command_id;
    f.commands.settle(device, hot, true, 0, f.clock.now());
    f.set_load(device, 50.0); // 온도를 낮춘다. 부하 상태는 여전히 미정이다.
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent.back().mode, Mode::normal);
    f.clock.advance(6s);
    f.commands.sweep(f.clock.now());
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 3u);
    EXPECT_EQ(f.sender.sent.back().mode, Mode::normal);
}

TEST(PolicyServiceTest, ReleaseDuringDispatchDoesNotRestoreOldBelief) {
    PolicyFixture f;
    auto const device = f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.sender.on_send = [&] { f.policy.on_device_released(device); };
    f.policy.evaluate(f.clock.now());
    f.sender.on_send = {};
    f.policy.evaluate(f.clock.now()); // 같은 Device가 다시 연결된 상황을 평가한다.
    EXPECT_EQ(f.sender.sent.size(), 2u);
}

TEST(PolicyServiceTest, DestroyedPolicyDoesNotReceivePendingCommandFailure) {
    ManualClock clock;
    FakeActiveDevices active;
    DeviceRegistry devices;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 1, 500ms};
    auto const device = make_device_id(1);
    devices.enroll(device, "sensors");
    ASSERT_TRUE(
        devices.update_status(device, Status{.mode = Mode::normal, .load = 90.0, .temp = 40.0})
    );
    active.active.push_back(device);
    {
        PolicyService policy{active, devices, commands};
        policy.set_policy(PolicyFixture::sensors_policy());
        policy.evaluate(clock.now());
    }
    clock.advance(6s);
    commands.sweep(clock.now());
    EXPECT_EQ(commands.metrics().failed_exhausted_total, 1u);
    EXPECT_EQ(commands.pending_count(), 0u);
}

TEST(PolicyServiceTest, StaysWithinHysteresisBand) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now()); // busy 전환

    f.set_load(id, 50.0); // low(20) < 50 < high(80): 밴드 안
    f.policy.evaluate(f.clock.now());

    EXPECT_EQ(f.sender.sent.size(), 1u); // 유지. 재발신 없음
}

TEST(PolicyServiceTest, ReturnsToIdleBelowLowLoad) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now()); // busy 전환

    f.set_load(id, 10.0);
    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[1].mode, Mode::normal); // idle_mode로 복귀
}

TEST(PolicyServiceTest, SetPolicyResetsRegime) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 90.0);
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now()); // busy 전환

    f.policy.set_policy(PolicyFixture::sensors_policy()); // 정책을 다시 적용해 명령 기록을 비운다. 부하 상태는 유지한다.
    f.policy.evaluate(f.clock.now());

    EXPECT_EQ(f.sender.sent.size(), 2u); // 같은 상태에서도 새 정책에 따라 명령을 다시 발행한다.
}

TEST(PolicyServiceTest, ExcludesInactiveDevicesFromAggregationAndCommands) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 10.0);
    f.enroll(0x02, "sensors", 100.0, /*active=*/false); // 연결이 끊긴 Device의 마지막 상태
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 1u); // 활성 Device의 부하 10만 집계하고 해당 Device에만 명령한다.
    EXPECT_EQ(f.sender.sent[0].device, make_device_id(0x01));
    EXPECT_EQ(f.sender.sent[0].mode, Mode::normal);
}

TEST(PolicyServiceTest, SkipsGroupWithoutActiveDevices) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 90.0, /*active=*/false);
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    EXPECT_TRUE(f.sender.sent.empty());
}

TEST(PolicyServiceTest, DeviceLeftClearsBeliefSoReconnectRecommands) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 90.0); // busy -> busy_mode(safe)
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 1u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::safe);

    // Device 연결이 종료되면 해당 Device의 명령 기록을 제거한다.
    f.policy.on_device_released(id);

    // 같은 ID로 다시 연결하면 목표가 이전과 같아도 명령을 다시 발행한다.
    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[1].device, id);
    EXPECT_EQ(f.sender.sent[1].mode, Mode::safe);
}

// 정책을 다시 적용해도 과열 상태는 해제 온도에 도달할 때까지 유지한다.
TEST(PolicyServiceTest, ReloadPreservesThermalLatchInDeadband) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 90.0); // load busy(>80)
    f.policy.set_policy(
        PolicyFixture::hot_policy()
    ); // busy=safe / thermal hot->performance(high90/resume70)

    // 온도 95가 과열 기준 90을 넘어 performance 모드로 전환한다.
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::performance, .load = 90.0, .temp = 95.0})
    );
    f.policy.evaluate(f.clock.now());
    ASSERT_FALSE(f.sender.sent.empty());
    EXPECT_EQ(f.sender.sent.back().mode, Mode::performance);

    // 온도를 80으로 낮춘다. 해제 기준 70보다 높으므로 과열 상태를 유지한다.
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::performance, .load = 90.0, .temp = 80.0})
    );

    auto const before = f.sender.sent.size();
    f.policy.set_policy(PolicyFixture::hot_policy()); // 핫리로드(같은 정책)
    f.policy.evaluate(f.clock.now());

    ASSERT_GT(f.sender.sent.size(), before);                 // 명령 기록이 초기화되어 다시 발행한다.
    EXPECT_EQ(f.sender.sent.back().mode, Mode::performance); // 과열 상태가 유지되어 hot_mode를 적용한다.
}

// 정책을 다시 적용하면 기존 부하 상태를 기준으로 새 모드를 선택한다.
TEST(PolicyServiceTest, ReloadAppliesNewModeToDeadbandGroup) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 90.0); // busy(>80) -> safe
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    ASSERT_FALSE(f.sender.sent.empty());
    EXPECT_EQ(f.sender.sent.back().mode, Mode::safe);

    f.set_load(id, 50.0); // 부하가 20과 80 사이이므로 기존 busy 상태를 유지한다.

    // busy mode를 safe -> normal로 바꾼 정책으로 reload
    GroupPolicy changed;
    changed.set("sensors", GroupRule::create(80.0, 20.0, Mode::normal, Mode::normal).value());
    auto const before = f.sender.sent.size();
    f.policy.set_policy(std::move(changed));
    f.policy.evaluate(f.clock.now());

    ASSERT_GT(f.sender.sent.size(), before);            // 기존 부하 상태를 유지하면서 새 모드로 명령한다.
    EXPECT_EQ(f.sender.sent.back().mode, Mode::normal); // 새 busy_mode 적용
}

TEST(PolicyServiceTest, ParsePolicyBuildsGroupPolicy) {
    auto const j = json::parse(
        R"({"groups":{"sensors":{"busy_load":80,"idle_load":20,)"
        R"("busy_mode":"safe","idle_mode":"normal"}}})"
    );
    ASSERT_TRUE(j.has_value());

    auto const p = parse_policy(*j);

    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->size(), 1u);
    p->for_each([](std::string const& group, GroupRule const& rule) {
        EXPECT_EQ(group, "sensors");
        EXPECT_EQ(rule.busy_load(), 80.0);
        EXPECT_EQ(rule.idle_load(), 20.0);
        EXPECT_EQ(rule.busy_mode(), Mode::safe);
        EXPECT_EQ(rule.idle_mode(), Mode::normal);
    });
}

TEST(PolicyServiceTest, ParsePolicyRejectsInvalidInput) {
    EXPECT_FALSE(parse_policy(*json::parse(R"({"x":1})")).has_value()); // groups 없음
    // 필드 누락
    EXPECT_FALSE(parse_policy(*json::parse(R"({"groups":{"s":{"busy_load":80}}})")).has_value());

    // 지원하지 않는 모드
    EXPECT_FALSE(parse_policy(*json::parse(
                                  R"({"groups":{"s":{"busy_load":80,"idle_load":20,)"
                                  R"("busy_mode":"warp","idle_mode":"normal"}}})"
                              ))
                     .has_value());
    // 상한이 하한보다 작은 잘못된 임계값
    EXPECT_FALSE(parse_policy(*json::parse(
                                  R"({"groups":{"s":{"busy_load":20,"idle_load":80,)"
                                  R"("busy_mode":"safe","idle_mode":"normal"}}})"
                              ))
                     .has_value());
    // 상한과 하한이 같은 잘못된 임계값
    EXPECT_FALSE(parse_policy(*json::parse(
                                  R"({"groups":{"s":{"busy_load":50,"idle_load":50,)"
                                  R"("busy_mode":"safe","idle_mode":"normal"}}})"
                              ))
                     .has_value());
}

TEST(PolicyServiceTest, ThermalOverrideWinsOverLoadRegime) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 95.0); // busy load
    f.policy.set_policy(PolicyFixture::hot_policy());
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::normal, .load = 95.0, .temp = 95.0})
    );

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 1u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // 과열 모드를 부하에 따른 safe 모드보다 우선한다.
}

// 과열된 Device에만 hot_mode를 적용하고, 나머지는 Group 부하에 따른 모드를 사용한다.
TEST(PolicyServiceTest, ThermalIsPerDevice) {
    PolicyFixture f;
    DeviceId const cool = f.enroll(0x01, "sensors", 10.0); // temp 40, idle load
    DeviceId const hot = f.enroll(0x02, "sensors", 10.0);
    f.policy.set_policy(PolicyFixture::hot_policy());
    EXPECT_TRUE(
        f.devices.update_status(hot, Status{.mode = Mode::normal, .load = 10.0, .temp = 95.0})
    );

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    Mode hot_mode = Mode::safe;
    Mode cool_mode = Mode::safe;
    for (auto const& s : f.sender.sent) {
        if (s.device == hot) {
            hot_mode = s.mode;
        } else if (s.device == cool) {
            cool_mode = s.mode;
        }
    }
    EXPECT_EQ(hot_mode, Mode::performance); // 뜨거운 device만 hot_mode
    EXPECT_EQ(cool_mode, Mode::normal);     // 나머지는 group load mode (idle -> idle_mode)
}

TEST(PolicyServiceTest, ThermalReleasesToLoadModeBelowResume) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 95.0); // busy load 유지
    f.policy.set_policy(PolicyFixture::hot_policy());
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::normal, .load = 95.0, .temp = 95.0})
    );
    f.policy.evaluate(f.clock.now()); // 과열 -> performance override

    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::normal, .load = 95.0, .temp = 60.0})
    );
    f.policy.evaluate(f.clock.now()); // cool_temp(70) 아래로 식음 -> busy load 모드 복귀

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // hot
    EXPECT_EQ(f.sender.sent[1].mode, Mode::safe);        // 해제 후 busy load_mode
}

// 부하 상태가 미정이어도 과열이 해제되면 기본 모드(idle_mode)로 복귀해야 한다.
TEST(PolicyServiceTest, ThermalReleasesToBaselineWhenLoadInBand) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 50.0); // 부하가 두 임계값 사이에 있어 초기 부하 상태가 미정이다.
    f.policy.set_policy(PolicyFixture::hot_policy());
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::normal, .load = 50.0, .temp = 95.0})
    );
    f.policy.evaluate(f.clock.now()); // 과열 상태로 전환해 performance 모드를 적용한다.

    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::normal, .load = 50.0, .temp = 60.0})
    );
    f.policy.evaluate(f.clock.now()); // cool_temp(70) 아래 + regime 미확정 -> baseline 복귀

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // hot
    EXPECT_EQ(f.sender.sent[1].mode, Mode::normal);      // 과열 상태가 해제되어 idle_mode로 복귀한다.

    f.policy.evaluate(f.clock.now());
    EXPECT_EQ(f.sender.sent.size(), 2u); // 해제 후 재발신 없음
}

// 전송 실패로 연결이 종료되면 활성 Device 목록이 바뀔 수 있다.
// 명령은 대상을 모으는 순회가 끝난 뒤 전송해야 한다.
TEST(PolicyServiceTest, DispatchesCommandsOutsideRosterIteration) {
    WindowedActiveDevices active_devices;
    DeviceRegistry devices;
    IterationProbeCommandSender sender{active_devices};
    CommandService commands{sender, 5s, 1, 500ms};
    PolicyService policy{active_devices, devices, commands};

    DeviceId const id1 = make_device_id(0x01);
    DeviceId const id2 = make_device_id(0x02);
    for (DeviceId const id :
         {id1, id2}) { // sensors의 활성 Device 2개의 평균 부하가 상한을 넘어 busy로 전환한다.
        devices.enroll(id, "sensors");
        EXPECT_TRUE(
            devices.update_status(id, Status{.mode = Mode::normal, .load = 95.0, .temp = 40.0})
        );
        active_devices.active.push_back(id);
    }
    policy.set_policy(PolicyFixture::sensors_policy());

    ManualClock clock;
    policy.evaluate(clock.now());

    ASSERT_EQ(sender.sent_count, 2); // busy 전환 시 Group의 두 Device에 명령을 보냈다.
    EXPECT_FALSE(sender.dispatched_during_iteration); // 발송은 순회 밖에서만
}

} // namespace
