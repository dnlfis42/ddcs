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

// 고정된 active 집합을 내주는 대역.
class FakeActiveDevices final : public ActiveDevices {
public:
    std::vector<DeviceId> active;

    void for_each_active(std::function<void(DeviceId)> const& fn) override {
        for (auto const id : active) {
            fn(id);
        }
    }
};

// 송신 의뢰를 기록하는 대역.
class FakeCommandSender final : public CommandSender {
public:
    struct Sent {
        DeviceId device;
        CommandId command_id;
        Mode mode; // SetMode 명령 값
    };

    std::vector<Sent> sent;

    SendResult send(DeviceId device, CommandId command_id, Command const& command) override {
        sent.push_back(
            Sent{
                .device = device,
                .command_id = command_id,
                .mode =
                    ddcs::device::decode_mode(std::get<SetMode>(command).mode).value_or(Mode::safe),
            }
        );
        return SendResult::ok;
    }
};

// 재진입 회귀용 대역. for_each_active의 순회 창(iterating)을 노출한다.
// 대역 자체는 UB를 피하려 스냅샷을 순회하되, 창이 열린 동안 송신이 일어났는지는 sender가 관측한다.
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

// 송신 시점에 active 집합을 순회 중이었는지 기록하는 대역
// dispatch가 순회 밖이면 iterating은 항상 false다.
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
    CommandService commands{sender, 5s, 1, 500ms};
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

    // load: busy=safe / idle=normal | thermal: hot -> performance (hot 90 / cool 70)
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
    f.enroll(0x03, "pumps", 99.0); // 정책 없는 그룹은 무관
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u); // sensors 멤버 전원, pumps 제외
    for (auto const& s : f.sender.sent) {
        EXPECT_EQ(s.mode, Mode::safe); // 계열이 SetMode인 것은 typed 명령이 보장
    }
    EXPECT_EQ(f.commands.pending_count(), 2u); // 전달 추적은 CommandService로 넘어갔다.
}

TEST(PolicyServiceTest, DefersUnobservedDeviceUntilFirstStatus) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 95.0);
    // 등록만 되고 보고 전인 device: 평균을 희석하지도, 명령을 받지도 않는다
    DeviceId const fresh = make_device_id(0x02);
    f.devices.enroll(fresh, "sensors");
    f.active_devices.active.push_back(fresh);
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 1u); // avg는 95(관측분만) -> busy. 미관측이 섞이면 47.5였다.
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
    f.policy.evaluate(f.clock.now()); // 같은 regime 재평가

    EXPECT_EQ(f.sender.sent.size(), 1u); // 전환마다 1회
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

    f.policy.set_policy(PolicyFixture::sensors_policy()); // 핫리로드 등가. regime 리셋
    f.policy.evaluate(f.clock.now());

    EXPECT_EQ(f.sender.sent.size(), 2u); // 같은 조건이라도 재전환 발신
}

TEST(PolicyServiceTest, ExcludesInactiveDevicesFromAggregationAndCommands) {
    PolicyFixture f;
    f.enroll(0x01, "sensors", 10.0);
    f.enroll(0x02, "sensors", 100.0, /*active=*/false); // 끊긴 device의 stale Shadow
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 1u); // 평균은 active만(10)이라 idle 전환. 명령도 active에게만
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

    // device가 세션을 잃었다(재시작 등). 컨트롤러의 per-device 명령 belief를 폐기한다.
    f.policy.on_device_released(id);

    // 같은 id로 재접속(agent는 normal로 리부트). regime/effective는 그대로 safe지만,
    // belief가 비었으니 재명령해야 한다. (안 비웠다면 commanded==effective라 suppress돼 1로
    // 남는다.)
    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[1].device, id);
    EXPECT_EQ(f.sender.sent[1].mode, Mode::safe);
}

// 핫리로드(set_policy 재적용)가 과열 latch를 보존: 데드밴드에서 식는 중 리로드해도 조기 해제 X.
TEST(PolicyServiceTest, ReloadPreservesThermalLatchInDeadband) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 90.0); // load busy(>80)
    f.policy.set_policy(
        PolicyFixture::hot_policy()
    ); // busy=safe / thermal hot->performance(high90/resume70)

    // 과열 트립(temp 95 > high 90) -> hot_mode(performance)
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::performance, .load = 90.0, .temp = 95.0})
    );
    f.policy.evaluate(f.clock.now());
    ASSERT_FALSE(f.sender.sent.empty());
    EXPECT_EQ(f.sender.sent.back().mode, Mode::performance);

    // 데드밴드로 식힘(resume70 < temp80 < high90), load는 busy 유지
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::performance, .load = 90.0, .temp = 80.0})
    );

    auto const before = f.sender.sent.size();
    f.policy.set_policy(PolicyFixture::hot_policy()); // 핫리로드(같은 정책)
    f.policy.evaluate(f.clock.now());

    ASSERT_GT(f.sender.sent.size(), before);                 // commanded clear로 재명령은 나감
    EXPECT_EQ(f.sender.sent.back().mode, Mode::performance); // latch 보존 -> 여전히 hot_mode
}

// 핫리로드가 regime latch를 보존: 부하가 데드밴드인 group의 mode 변경도 즉시 적용된다.
TEST(PolicyServiceTest, ReloadAppliesNewModeToDeadbandGroup) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 90.0); // busy(>80) -> safe
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    ASSERT_FALSE(f.sender.sent.empty());
    EXPECT_EQ(f.sender.sent.back().mode, Mode::safe);

    f.set_load(id, 50.0); // 데드밴드(20<50<80) -- regime은 busy로 latch 유지

    // busy mode를 safe -> normal로 바꾼 정책으로 reload
    GroupPolicy changed;
    changed.set("sensors", GroupRule::create(80.0, 20.0, Mode::normal, Mode::normal).value());
    auto const before = f.sender.sent.size();
    f.policy.set_policy(std::move(changed));
    f.policy.evaluate(f.clock.now());

    ASSERT_GT(f.sender.sent.size(), before);            // regime 보존이라 데드밴드에서도 재명령
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

    // 미지 mode
    EXPECT_FALSE(parse_policy(*json::parse(
                                  R"({"groups":{"s":{"busy_load":80,"idle_load":20,)"
                                  R"("busy_mode":"warp","idle_mode":"normal"}}})"
                              ))
                     .has_value());
    // 임계 역전 시 발진
    EXPECT_FALSE(parse_policy(*json::parse(
                                  R"({"groups":{"s":{"busy_load":20,"idle_load":80,)"
                                  R"("busy_mode":"safe","idle_mode":"normal"}}})"
                              ))
                     .has_value());
    // 밴드 없음
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
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // hot이 busy(safe)를 이김
}

// per-device thermal: 한 device만 과열하면 그 device만 hot_mode, 나머지는 group load mode.
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

// 회귀: load가 밴드 안(regime 미확정)에서 thermal 트립 후 식으면, hot_mode가 latch된 채
//       남지 않고 baseline(idle_mode)으로 해제돼야 한다.
TEST(PolicyServiceTest, ThermalReleasesToBaselineWhenLoadInBand) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 50.0); // 20 < 50 < 80: 밴드 안 -> regime unknown
    f.policy.set_policy(PolicyFixture::hot_policy());
    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::normal, .load = 50.0, .temp = 95.0})
    );
    f.policy.evaluate(f.clock.now()); // 과열 -> hot_mode(performance) latch

    EXPECT_TRUE(
        f.devices.update_status(id, Status{.mode = Mode::normal, .load = 50.0, .temp = 60.0})
    );
    f.policy.evaluate(f.clock.now()); // cool_temp(70) 아래 + regime 미확정 -> baseline 복귀

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // hot
    EXPECT_EQ(f.sender.sent[1].mode, Mode::normal);      // idle_mode 복귀 (비상모드 latch 해제)

    f.policy.evaluate(f.clock.now());
    EXPECT_EQ(f.sender.sent.size(), 2u); // 해제 후 재발신 없음
}

// 회귀: dispatch는 송신 실패 시 동기 disconnect로 active 집합을 순회 중 변형할 수 있다.
//       command_group은 대상을 모은 뒤 순회 밖에서 발송해야 한다.
TEST(PolicyServiceTest, DispatchesCommandsOutsideRosterIteration) {
    WindowedActiveDevices active_devices;
    DeviceRegistry devices;
    IterationProbeCommandSender sender{active_devices};
    CommandService commands{sender, 5s, 1, 500ms};
    PolicyService policy{active_devices, devices, commands};

    DeviceId const id1 = make_device_id(0x01);
    DeviceId const id2 = make_device_id(0x02);
    for (DeviceId const id :
         {id1, id2}) { // sensors 그룹 active 2개, 평균 load > high라서 busy 전환
        devices.enroll(id, "sensors");
        EXPECT_TRUE(
            devices.update_status(id, Status{.mode = Mode::normal, .load = 95.0, .temp = 40.0})
        );
        active_devices.active.push_back(id);
    }
    policy.set_policy(PolicyFixture::sensors_policy());

    ManualClock clock;
    policy.evaluate(clock.now());

    ASSERT_EQ(sender.sent_count, 2); // busy 전환에서 그룹 전원에 발신됐다(경로가 실제로 탔다).
    EXPECT_FALSE(sender.dispatched_during_iteration); // 발송은 순회 밖에서만
}

} // namespace
