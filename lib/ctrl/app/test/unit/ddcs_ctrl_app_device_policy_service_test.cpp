#include "ddcs/ctrl/app/device/policy_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/command_buffer.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/app/device/port/device_roster.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/command.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"
#include "ddcs/json/value.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace cmd = ddcs::device;
namespace json = ddcs::json;

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::ObjectPool;
using ddcs::ctrl::app::device::CommandService;
using ddcs::ctrl::app::device::parse_policy;
using ddcs::ctrl::app::device::PolicyService;
using ddcs::ctrl::app::device::port::CommandBuffer;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::CommandSender;
using ddcs::ctrl::app::device::port::DeviceRoster;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::domain::GroupPolicy;
using ddcs::ctrl::domain::GroupRule;
using ddcs::device::Mode;
using namespace std::chrono_literals;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

// 고정된 active 집합을 내주는 대역.
class FakeDeviceRoster final : public DeviceRoster {
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
        std::uint8_t type;
        Mode mode; // SetMode payload decode 결과
    };

    std::vector<Sent> sent;

    CommandBuffer make_command_buffer() override { return pool_.acquire(); }

    bool try_send(DeviceId device, CommandId command_id, std::uint8_t command_type, CommandBuffer message) override {
        cmd::SetMode set_mode{};
        EXPECT_TRUE(cmd::decode(message->readable(), set_mode));
        sent.push_back(Sent{.device = device, .command_id = command_id, .type = command_type, .mode = set_mode.mode});
        return true;
    }

private:
    ObjectPool<LinearBuffer> pool_{ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{128})};
};

// 재진입 회귀용 대역. for_each_active의 순회 창(iterating)을 노출한다.
// 대역 자체는 UB를 피하려 스냅샷을 순회하되, 창이 열린 동안 송신이 일어났는지는 sender가 관측한다.
class WindowedDeviceRoster final : public DeviceRoster {
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

// 송신 시점에 roster가 순회 중이었는지 기록하는 대역. dispatch가 순회 밖이면 iterating은 항상 false다.
class IterationProbeCommandSender final : public CommandSender {
public:
    explicit IterationProbeCommandSender(WindowedDeviceRoster& roster) noexcept : roster_{roster} {}

    int sent_count = 0;
    bool dispatched_during_iteration = false;

    CommandBuffer make_command_buffer() override { return pool_.acquire(); }

    bool try_send(DeviceId, CommandId, std::uint8_t, CommandBuffer) override {
        if (roster_.iterating) {
            dispatched_during_iteration = true;
        }
        ++sent_count;
        return true;
    }

private:
    WindowedDeviceRoster& roster_;
    ObjectPool<LinearBuffer> pool_{ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{128})};
};

struct PolicyFixture {
    ManualClock clock;
    FakeDeviceRoster roster;
    DeviceRegistry devices;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 1, 500ms};
    PolicyService policy{roster, devices, commands};

    DeviceId enroll(std::uint8_t seed, std::string group, double load, bool active = true) {
        DeviceId const id = make_device_id(seed);
        devices.find_or_create(id);
        devices.set_group(id, std::move(group));
        devices.update_status(id, ddcs::device::Status{.mode = Mode::normal, .load = load, .temp = 40.0});
        if (active) {
            roster.active.push_back(id);
        }
        return id;
    }

    void set_load(DeviceId id, double load) {
        devices.update_status(id, ddcs::device::Status{.mode = Mode::normal, .load = load, .temp = 40.0});
    }

    static GroupPolicy sensors_policy() {
        GroupPolicy p;
        p.set("sensors", GroupRule::try_make(80.0, 20.0, Mode::safe, Mode::normal).value());
        return p;
    }
};

} // namespace

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
        EXPECT_EQ(s.type, static_cast<std::uint8_t>(cmd::CommandType::set_mode));
        EXPECT_EQ(s.mode, Mode::safe);
    }
    EXPECT_EQ(f.commands.pending_count(), 2u); // 전달 추적은 CommandService로 넘어갔다
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
    f.enroll(0x02, "sensors", 100.0, /*active=*/false); // 끊긴 device의 stale 트윈
    f.policy.set_policy(PolicyFixture::sensors_policy());

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 1u); // 평균은 active만(10) -> idle 전환. 명령도 active에게만
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

TEST(PolicyServiceTest, ParsePolicyBuildsGroupPolicy) {
    auto const j = json::Value::parse(
        R"({"groups":{"sensors":{"high_load":80,"low_load":20,"busy_mode":"safe","idle_mode":"normal"}}})"
    );
    ASSERT_TRUE(j.has_value());

    auto const p = parse_policy(*j);

    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->size(), 1u);
    p->for_each([](std::string const& group, GroupRule const& rule) {
        EXPECT_EQ(group, "sensors");
        EXPECT_EQ(rule.high_load(), 80.0);
        EXPECT_EQ(rule.low_load(), 20.0);
        EXPECT_EQ(rule.busy_mode(), Mode::safe);
        EXPECT_EQ(rule.idle_mode(), Mode::normal);
    });
}

TEST(PolicyServiceTest, ParsePolicyRejectsInvalidInput) {
    EXPECT_FALSE(parse_policy(*json::Value::parse(R"({"x":1})")).has_value()); // groups 없음
    EXPECT_FALSE(parse_policy(*json::Value::parse(R"({"groups":{"s":{"high_load":80}}})")).has_value()); // 필드 누락
    EXPECT_FALSE(
        parse_policy(*json::Value::parse(
                         R"({"groups":{"s":{"high_load":80,"low_load":20,"busy_mode":"warp","idle_mode":"normal"}}})"
                     ))
            .has_value()
    ); // 미지 mode
    EXPECT_FALSE(
        parse_policy(*json::Value::parse(
                         R"({"groups":{"s":{"high_load":20,"low_load":80,"busy_mode":"safe","idle_mode":"normal"}}})"
                     ))
            .has_value()
    ); // 임계 역전 -> 발진
    EXPECT_FALSE(
        parse_policy(*json::Value::parse(
                         R"({"groups":{"s":{"high_load":50,"low_load":50,"busy_mode":"safe","idle_mode":"normal"}}})"
                     ))
            .has_value()
    ); // 밴드 없음
}

// 회귀: dispatch는 송신 실패 시 동기 disconnect로 roster를 순회 중 변형할 수 있으므로
// (DeviceRoster 포트 CAUTION), command_group은 대상을 모은 뒤 순회 밖에서 발송해야 한다.
TEST(PolicyServiceTest, DispatchesCommandsOutsideRosterIteration) {
    WindowedDeviceRoster roster;
    DeviceRegistry devices;
    IterationProbeCommandSender sender{roster};
    CommandService commands{sender, 5s, 1, 500ms};
    PolicyService policy{roster, devices, commands};

    DeviceId const id1 = make_device_id(0x01);
    DeviceId const id2 = make_device_id(0x02);
    for (DeviceId const id : {id1, id2}) { // sensors 그룹 active 2개, 평균 load > high -> busy 전환
        devices.find_or_create(id);
        devices.set_group(id, "sensors");
        devices.update_status(id, ddcs::device::Status{.mode = Mode::normal, .load = 95.0, .temp = 40.0});
        roster.active.push_back(id);
    }
    policy.set_policy(PolicyFixture::sensors_policy());

    ManualClock clock;
    policy.evaluate(clock.now());

    ASSERT_EQ(sender.sent_count, 2); // busy 전환에서 그룹 전원에 발신됐다(경로가 실제로 탔다)
    EXPECT_FALSE(sender.dispatched_during_iteration); // 발송은 순회 밖에서만
}
