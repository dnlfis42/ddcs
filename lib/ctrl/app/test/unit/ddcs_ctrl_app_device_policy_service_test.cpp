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
#include "ddcs/ctrl/domain/status.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/wire/message/command.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace msg = ddcs::wire::message;
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
using ddcs::ctrl::domain::ThermalRule;
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

    CommandBuffer make_command_buffer() override {
        return pool_.acquire();
    }

    bool try_send(
        DeviceId device, CommandId command_id, std::uint8_t command_type, CommandBuffer message
    ) override {
        auto const set_mode = msg::decode_set_mode(message->data_span());
        EXPECT_TRUE(set_mode.has_value());
        sent.push_back(Sent{
            .device = device,
            .command_id = command_id,
            .type = command_type,
            .mode = ddcs::device::decode_mode(set_mode.value_or(msg::SetMode{}).mode)
                        .value_or(Mode::safe),
        });
        return true;
    }

private:
    ObjectPool<LinearBuffer> pool_{ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{128
    })};
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

// 송신 시점에 roster가 순회 중이었는지 기록하는 대역
// dispatch가 순회 밖이면 iterating은 항상 false다.
class IterationProbeCommandSender final : public CommandSender {
public:
    explicit IterationProbeCommandSender(WindowedDeviceRoster& roster) noexcept
        : roster_{roster} {}

    int sent_count = 0;
    bool dispatched_during_iteration = false;

    CommandBuffer make_command_buffer() override {
        return pool_.acquire();
    }

    bool try_send(DeviceId, CommandId, std::uint8_t, CommandBuffer) override {
        if (roster_.iterating) {
            dispatched_during_iteration = true;
        }
        ++sent_count;
        return true;
    }

private:
    WindowedDeviceRoster& roster_;
    ObjectPool<LinearBuffer> pool_{ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{128
    })};
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
        devices.update_status(
            id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = load, .temp = 40.0}
        );
        if (active) {
            roster.active.push_back(id);
        }
        return id;
    }

    void set_load(DeviceId id, double load) {
        devices.update_status(
            id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = load, .temp = 40.0}
        );
    }

    static GroupPolicy sensors_policy() {
        GroupPolicy p;
        p.set("sensors", GroupRule::try_make(80.0, 20.0, Mode::safe, Mode::normal).value());
        return p;
    }

    // load: busy=safe / idle=normal | thermal: hot -> performance (high 90 / resume 70)
    static GroupPolicy hot_policy() {
        GroupPolicy p;
        p.set(
            "sensors",
            GroupRule::try_make(
                80.0, 20.0, Mode::safe, Mode::normal,
                ThermalRule{
                    .high_temp = 90.0, .resume_temp = 70.0, .high_temp_mode = Mode::performance
                }
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
        EXPECT_EQ(s.type, static_cast<std::uint8_t>(msg::CommandType::set_mode));
        EXPECT_EQ(s.mode, Mode::safe);
    }
    EXPECT_EQ(f.commands.pending_count(), 2u); // 전달 추적은 CommandService로 넘어갔다.
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
    EXPECT_EQ(f.sender.sent[1].mode, Mode::normal); // low_load_mode로 복귀
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
    DeviceId const id = f.enroll(0x01, "sensors", 90.0); // busy -> high_load_mode(safe)
    f.policy.set_policy(PolicyFixture::sensors_policy());
    f.policy.evaluate(f.clock.now());
    ASSERT_EQ(f.sender.sent.size(), 1u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::safe);

    // device가 세션을 잃었다(재시작 등). 컨트롤러의 per-device 명령 belief를 폐기한다.
    f.policy.on_device_left(id);

    // 같은 id로 재접속(agent는 normal로 리부트). regime/effective는 그대로 safe지만,
    // belief가 비었으니 재명령해야 한다. (안 비웠다면 commanded==effective라 suppress돼 1로
    // 남는다.)
    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[1].device, id);
    EXPECT_EQ(f.sender.sent[1].mode, Mode::safe);
}

TEST(PolicyServiceTest, ParsePolicyBuildsGroupPolicy) {
    auto const j = json::parse(R"({"groups":{"sensors":{"high_load":80,"low_load":20,)"
                               R"("high_load_mode":"safe","low_load_mode":"normal"}}})");
    ASSERT_TRUE(j.has_value());

    auto const p = parse_policy(*j);

    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->size(), 1u);
    p->for_each([](std::string const& group, GroupRule const& rule) {
        EXPECT_EQ(group, "sensors");
        EXPECT_EQ(rule.high_load(), 80.0);
        EXPECT_EQ(rule.low_load(), 20.0);
        EXPECT_EQ(rule.high_load_mode(), Mode::safe);
        EXPECT_EQ(rule.low_load_mode(), Mode::normal);
    });
}

TEST(PolicyServiceTest, ParsePolicyRejectsInvalidInput) {
    EXPECT_FALSE(parse_policy(*json::parse(R"({"x":1})")).has_value()); // groups 없음
    // 필드 누락
    EXPECT_FALSE(parse_policy(*json::parse(R"({"groups":{"s":{"high_load":80}}})")).has_value());

    // 미지 mode
    EXPECT_FALSE(parse_policy(*json::parse(R"({"groups":{"s":{"high_load":80,"low_load":20,)"
                                           R"("high_load_mode":"warp","low_load_mode":"normal"}}})")
    )
                     .has_value());
    // 임계 역전 시 발진
    EXPECT_FALSE(parse_policy(*json::parse(R"({"groups":{"s":{"high_load":20,"low_load":80,)"
                                           R"("high_load_mode":"safe","low_load_mode":"normal"}}})")
    )
                     .has_value());
    // 밴드 없음
    EXPECT_FALSE(parse_policy(*json::parse(R"({"groups":{"s":{"high_load":50,"low_load":50,)"
                                           R"("high_load_mode":"safe","low_load_mode":"normal"}}})")
    )
                     .has_value());
}

TEST(PolicyServiceTest, ThermalOverrideWinsOverLoadRegime) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 95.0); // busy load
    f.policy.set_policy(PolicyFixture::hot_policy());
    f.devices.update_status(
        id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = 95.0, .temp = 95.0}
    );

    f.policy.evaluate(f.clock.now());

    ASSERT_EQ(f.sender.sent.size(), 1u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // hot이 busy(safe)를 이김
}

// per-device thermal: 한 device만 과열하면 그 device만 high_temp_mode, 나머지는 group load mode.
TEST(PolicyServiceTest, ThermalIsPerDevice) {
    PolicyFixture f;
    DeviceId const cool = f.enroll(0x01, "sensors", 10.0); // temp 40, idle load
    DeviceId const hot = f.enroll(0x02, "sensors", 10.0);
    f.policy.set_policy(PolicyFixture::hot_policy());
    f.devices.update_status(
        hot, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = 10.0, .temp = 95.0}
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
    EXPECT_EQ(hot_mode, Mode::performance); // 뜨거운 device만 high_temp_mode
    EXPECT_EQ(cool_mode, Mode::normal);     // 나머지는 group load mode (idle -> low_load_mode)
}

TEST(PolicyServiceTest, ThermalReleasesToLoadModeBelowResume) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 95.0); // busy load 유지
    f.policy.set_policy(PolicyFixture::hot_policy());
    f.devices.update_status(
        id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = 95.0, .temp = 95.0}
    );
    f.policy.evaluate(f.clock.now()); // 과열 -> performance override

    f.devices.update_status(
        id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = 95.0, .temp = 60.0}
    );
    f.policy.evaluate(f.clock.now()); // resume(70) 아래로 식음 -> busy load 모드 복귀

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // hot
    EXPECT_EQ(f.sender.sent[1].mode, Mode::safe);        // 해제 후 busy load_mode
}

// 회귀: load가 밴드 안(regime 미확정)에서 thermal 트립 후 식으면, high_temp_mode가 latch된 채
//       남지 않고 baseline(low_load_mode)으로 해제돼야 한다.
TEST(PolicyServiceTest, ThermalReleasesToBaselineWhenLoadInBand) {
    PolicyFixture f;
    DeviceId const id = f.enroll(0x01, "sensors", 50.0); // 20 < 50 < 80: 밴드 안 -> regime unknown
    f.policy.set_policy(PolicyFixture::hot_policy());
    f.devices.update_status(
        id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = 50.0, .temp = 95.0}
    );
    f.policy.evaluate(f.clock.now()); // 과열 -> high_temp_mode(performance) latch

    f.devices.update_status(
        id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = 50.0, .temp = 60.0}
    );
    f.policy.evaluate(f.clock.now()); // resume(70) 아래 + regime 미확정 -> baseline 복귀

    ASSERT_EQ(f.sender.sent.size(), 2u);
    EXPECT_EQ(f.sender.sent[0].mode, Mode::performance); // hot
    EXPECT_EQ(f.sender.sent[1].mode, Mode::normal); // low_load_mode 복귀 (비상모드 latch 해제)

    f.policy.evaluate(f.clock.now());
    EXPECT_EQ(f.sender.sent.size(), 2u); // 해제 후 재발신 없음
}

// 회귀: dispatch는 송신 실패 시 동기 disconnect로 roster를 순회 중 변형할 수 있다.
//       command_group은 대상을 모은 뒤 순회 밖에서 발송해야 한다.
TEST(PolicyServiceTest, DispatchesCommandsOutsideRosterIteration) {
    WindowedDeviceRoster roster;
    DeviceRegistry devices;
    IterationProbeCommandSender sender{roster};
    CommandService commands{sender, 5s, 1, 500ms};
    PolicyService policy{roster, devices, commands};

    DeviceId const id1 = make_device_id(0x01);
    DeviceId const id2 = make_device_id(0x02);
    for (DeviceId const id :
         {id1, id2}) { // sensors 그룹 active 2개, 평균 load > high라서 busy 전환
        devices.find_or_create(id);
        devices.set_group(id, "sensors");
        devices.update_status(
            id, ddcs::ctrl::domain::Status{.mode = Mode::normal, .load = 95.0, .temp = 40.0}
        );
        roster.active.push_back(id);
    }
    policy.set_policy(PolicyFixture::sensors_policy());

    ManualClock clock;
    policy.evaluate(clock.now());

    ASSERT_EQ(sender.sent_count, 2); // busy 전환에서 그룹 전원에 발신됐다(경로가 실제로 탔다).
    EXPECT_FALSE(sender.dispatched_during_iteration); // 발송은 순회 밖에서만
}

} // namespace
