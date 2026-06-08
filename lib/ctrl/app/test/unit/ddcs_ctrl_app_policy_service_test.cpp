#include "ddcs/ctrl/app/policy/policy_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/ops/operator_service.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/proto/cmd/command.hpp"
#include "ddcs/proto/msg/message.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::app::ops::OperatorService;
using ddcs::ctrl::app::policy::parse_policy;
using ddcs::ctrl::app::policy::PolicyService;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::domain::GroupPolicy;
using ddcs::ctrl::domain::GroupRule;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;
using ddcs::device::Mode;
namespace cmd = ddcs::proto::cmd;
namespace json = ddcs::json;
namespace msg = ddcs::proto::msg;

class MockOutbound : public Outbound {
public:
    ddcs::common::ObjectPool<LinearBuffer> pool{ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{256})};
    std::vector<std::string> sends; // 캡처된 Command body

    PoolHandle<LinearBuffer> send_buffer() override { return pool.acquire(); }
    void send(ConnectionId, std::uint8_t, PoolHandle<LinearBuffer> body) override {
        auto const r = body->readable();
        sends.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }
    void drop(ConnectionId) override {}
};

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return Uuid{b};
}

// 캡처된 Command body -> SetMode.mode 디코드.
Mode mode_of(std::string const& command_body) {
    msg::Command c{};
    std::span<std::byte const> payload{};
    EXPECT_TRUE(
        msg::decode({reinterpret_cast<std::byte const*>(command_body.data()), command_body.size()}, c, payload)
    );
    cmd::SetMode sm{};
    EXPECT_TRUE(cmd::decode(payload, sm));
    return sm.mode;
}

GroupRule sensors_rule() {
    return GroupRule{.high_load = 80, .low_load = 40, .busy_mode = Mode::performance, .idle_mode = Mode::safe};
}

struct Fixture {
    SessionRegistry sessions;
    DeviceRegistry registry;
    MockOutbound outbound;
    ManualClock clock;
    CommandService commands{sessions, outbound, clock, std::chrono::seconds{5}};
    OperatorService ops{registry, commands};
    PolicyService policy{sessions, registry, ops};

    DeviceId add_agent(std::uint8_t seed, ConnectionId conn, std::string const& group, double load) {
        auto const id = registry.find_or_create(make_uuid(seed)).id;
        registry.set_group(id, group);
        registry.update_status(id, ddcs::device::Status{.mode = Mode::normal, .load = load, .temp = 0.0});
        sessions.open(conn);
        sessions.bind(conn, id, {});
        return id;
    }

    void set_single_group_policy() {
        GroupPolicy p;
        p.set("sensors", sensors_rule());
        policy.set_policy(std::move(p));
    }
};

} // namespace

// --- parse -------------------------------------------------------------------
TEST(PolicyServiceTest, ParsesValidPolicy) {
    auto const j = json::Value::parse(
        R"({"groups":{"sensors":{"high_load":80,"low_load":40,"busy_mode":"performance","idle_mode":"safe"}}})"
    );
    ASSERT_TRUE(j.has_value());
    auto const p = parse_policy(*j);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->size(), 1u);

    GroupRule got{};
    bool found = false;
    p->for_each([&](std::string const& g, GroupRule const& r) {
        if (g == "sensors") {
            got = r;
            found = true;
        }
    });
    ASSERT_TRUE(found);
    EXPECT_EQ(got.high_load, 80.0);
    EXPECT_EQ(got.low_load, 40.0);
    EXPECT_EQ(got.busy_mode, Mode::performance);
    EXPECT_EQ(got.idle_mode, Mode::safe);
}

TEST(PolicyServiceTest, RejectsMalformedPolicy) {
    EXPECT_FALSE(parse_policy(*json::Value::parse(R"({"x":1})")).has_value());                           // groups 없음
    EXPECT_FALSE(parse_policy(*json::Value::parse(R"({"groups":{"s":{"high_load":80}}})")).has_value()); // 필드 누락
    EXPECT_FALSE(
        parse_policy(*json::Value::parse(
                         R"({"groups":{"s":{"high_load":80,"low_load":40,"busy_mode":"perf","idle_mode":"safe"}}})"
                     ))
            .has_value()
    ); // 미지 mode "perf"
}

// --- evaluate (히스테리시스) -------------------------------------------------
TEST(PolicyServiceTest, CommandsBusyModeWhenGroupLoadExceedsHigh) {
    Fixture f;
    f.set_single_group_policy();
    f.add_agent(1, ConnectionId{1}, "sensors", 90); // avg = 90 > 80
    f.add_agent(2, ConnectionId{2}, "sensors", 90);

    f.policy.evaluate();

    ASSERT_EQ(f.outbound.sends.size(), 2u); // 그룹의 두 agent 에 SetMode
    EXPECT_EQ(mode_of(f.outbound.sends[0]), Mode::performance);
    EXPECT_EQ(mode_of(f.outbound.sends[1]), Mode::performance);
}

TEST(PolicyServiceTest, HoldsWhenLoadInHysteresisBand) {
    Fixture f;
    f.set_single_group_policy();
    f.add_agent(1, ConnectionId{1}, "sensors", 60); // 40 < 60 < 80 -> 밴드

    f.policy.evaluate();
    EXPECT_TRUE(f.outbound.sends.empty()); // 전환 없음
}

TEST(PolicyServiceTest, DoesNotReCommandWhileInSameRegime) {
    Fixture f;
    f.set_single_group_policy();
    f.add_agent(1, ConnectionId{1}, "sensors", 90);

    f.policy.evaluate(); // busy 전환 -> 1 command
    ASSERT_EQ(f.outbound.sends.size(), 1u);
    f.outbound.sends.clear();

    f.policy.evaluate(); // 여전히 busy -> 재발송 없음
    EXPECT_TRUE(f.outbound.sends.empty());
}

TEST(PolicyServiceTest, ReturnsToIdleModeAfterRecovery) {
    Fixture f;
    f.set_single_group_policy();
    auto const id = f.add_agent(1, ConnectionId{1}, "sensors", 90);

    f.policy.evaluate(); // busy
    f.outbound.sends.clear();

    f.registry.update_status(
        id, ddcs::device::Status{.mode = Mode::performance, .load = 30.0, .temp = 0.0}
    ); // load 30 < 40 -> 회복
    f.policy.evaluate();

    ASSERT_EQ(f.outbound.sends.size(), 1u);
    EXPECT_EQ(mode_of(f.outbound.sends[0]), Mode::safe); // idle_mode 로 복귀
}

TEST(PolicyServiceTest, IgnoresAgentsOutsidePolicyGroups) {
    Fixture f;
    f.set_single_group_policy();
    f.add_agent(1, ConnectionId{1}, "actuators", 99); // 정책에 없는 그룹

    f.policy.evaluate();
    EXPECT_TRUE(f.outbound.sends.empty());
}

TEST(PolicyServiceTest, EmptyPolicyIsNoOp) {
    Fixture f; // set_policy 안 함 -> 빈 정책
    f.add_agent(1, ConnectionId{1}, "sensors", 99);

    f.policy.evaluate();
    EXPECT_TRUE(f.outbound.sends.empty());
}
