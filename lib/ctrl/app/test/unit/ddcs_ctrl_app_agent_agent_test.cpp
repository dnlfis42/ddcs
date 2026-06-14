#include "ddcs/ctrl/app/agent/agent.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::ctrl::app::agent::Agent;
using ddcs::ctrl::app::agent::port::ConnectionId;
using ddcs::ctrl::domain::DeviceId;
using namespace std::chrono_literals;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

} // namespace

TEST(AgentTest, DefaultConstructedIsIdle) {
    Agent const agent;

    EXPECT_FALSE(agent.valid());
    EXPECT_EQ(agent.state(), Agent::State::idle);
}

TEST(AgentTest, ConstructionStartsHandshaking) {
    ManualClock clock;
    clock.advance(10s);
    Agent const agent{ConnectionId{7}, clock.now()};

    EXPECT_TRUE(agent.valid());
    EXPECT_EQ(agent.state(), Agent::State::handshaking);
    EXPECT_EQ(agent.conn(), ConnectionId{7});
    EXPECT_EQ(agent.last_seen(), clock.now());
    EXPECT_FALSE(agent.device().valid()); // 등록 전엔 바인딩 없음
}

TEST(AgentTest, BindTransitionsToConfirmingAndUpdatesSeen) {
    ManualClock clock;
    Agent agent{ConnectionId{7}, clock.now()};

    clock.advance(3s);
    ASSERT_TRUE(agent.bind(make_device_id(0xAB), clock.now()));

    EXPECT_EQ(agent.state(), Agent::State::confirming); // active는 RegisterAck 이후
    EXPECT_EQ(agent.device(), make_device_id(0xAB));
    EXPECT_EQ(agent.last_seen(), clock.now()); // ack 대기 구간이 새 budget을 받는다
}

TEST(AgentTest, ConfirmTransitionsToActiveAndUpdatesSeen) {
    ManualClock clock;
    Agent agent{ConnectionId{7}, clock.now()};
    ASSERT_TRUE(agent.bind(make_device_id(0xAB), clock.now()));

    clock.advance(2s);
    ASSERT_TRUE(agent.confirm(clock.now()));

    EXPECT_EQ(agent.state(), Agent::State::active);
    EXPECT_EQ(agent.last_seen(), clock.now()); // liveness 측정 시작점 = ack 수신
}

TEST(AgentTest, ConfirmRejectsWhenNotConfirming) {
    ManualClock clock;

    Agent idle_agent;
    EXPECT_FALSE(idle_agent.confirm(clock.now()));

    Agent agent{ConnectionId{7}, clock.now()};
    EXPECT_FALSE(agent.confirm(clock.now())); // handshaking: bind 전 ack는 무효

    ASSERT_TRUE(agent.bind(make_device_id(0x01), clock.now()));
    ASSERT_TRUE(agent.confirm(clock.now()));
    EXPECT_FALSE(agent.confirm(clock.now())); // active: 중복 ack는 무효
}

TEST(AgentTest, BindRejectsNilDevice) {
    ManualClock clock;
    Agent agent{ConnectionId{7}, clock.now()};

    EXPECT_FALSE(agent.bind(DeviceId{}, clock.now()));
    EXPECT_EQ(agent.state(), Agent::State::handshaking);
}

TEST(AgentTest, BindRejectsWhenNotHandshaking) {
    ManualClock clock;

    Agent idle_agent;
    EXPECT_FALSE(idle_agent.bind(make_device_id(0x01), clock.now()));

    Agent agent{ConnectionId{7}, clock.now()};
    ASSERT_TRUE(agent.bind(make_device_id(0x01), clock.now()));
    EXPECT_FALSE(agent.bind(make_device_id(0x02), clock.now())); // 재바인딩 금지
    EXPECT_EQ(agent.device(), make_device_id(0x01));
}

TEST(AgentTest, UpdateSeenRefreshesLastSeen) {
    ManualClock clock;
    Agent agent{ConnectionId{7}, clock.now()};

    clock.advance(5s);
    agent.update_seen(clock.now());

    EXPECT_EQ(agent.last_seen(), clock.now());
}
