#include "ddcs/ctrl/app/agent/agent_registry.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::ctrl::app::agent::Agent;
using ddcs::ctrl::app::agent::AgentRegistry;
using ddcs::ctrl::app::agent::port::ConnectionId;
using ddcs::ctrl::domain::DeviceId;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

} // namespace

TEST(AgentRegistryTest, AddCreatesHandshakingAgent) {
    ManualClock clock;
    AgentRegistry registry;

    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));

    Agent* agent = registry.find(ConnectionId{1});
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->state(), Agent::State::handshaking);
    EXPECT_EQ(registry.size(), 1u);
}

TEST(AgentRegistryTest, AddRejectsDuplicateConnection) {
    ManualClock clock;
    AgentRegistry registry;

    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));
    EXPECT_FALSE(registry.add(ConnectionId{1}, clock.now()));
    EXPECT_EQ(registry.size(), 1u);
}

TEST(AgentRegistryTest, BindIndexesDeviceAndTransitionsToConfirming) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));

    ASSERT_TRUE(registry.bind(ConnectionId{1}, make_device_id(0xAA), clock.now()));

    Agent* by_conn = registry.find(ConnectionId{1});
    Agent* by_device = registry.find(make_device_id(0xAA));
    ASSERT_NE(by_conn, nullptr);
    EXPECT_EQ(by_conn, by_device); // 역색인이 같은 엔티티에 닿는다(ack 전에도 device 점유).
    EXPECT_EQ(by_conn->state(), Agent::State::confirming);
}

TEST(AgentRegistryTest, BindRejectsUnknownConnection) {
    ManualClock clock;
    AgentRegistry registry;

    EXPECT_FALSE(registry.bind(ConnectionId{9}, make_device_id(0xAA), clock.now()));
    EXPECT_EQ(registry.find(make_device_id(0xAA)), nullptr);
}

TEST(AgentRegistryTest, BindRejectsOccupiedDevice) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));
    ASSERT_TRUE(registry.add(ConnectionId{2}, clock.now()));
    ASSERT_TRUE(registry.bind(ConnectionId{1}, make_device_id(0xAA), clock.now()));

    EXPECT_FALSE(registry.bind(ConnectionId{2}, make_device_id(0xAA), clock.now()));

    // 기존 바인딩 유지
    EXPECT_EQ(registry.find(make_device_id(0xAA)), registry.find(ConnectionId{1}));
    EXPECT_EQ(registry.find(ConnectionId{2})->state(), Agent::State::handshaking); // 상태 불변
}

TEST(AgentRegistryTest, BindRejectsNilDeviceWithoutIndexResidue) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));

    EXPECT_FALSE(registry.bind(ConnectionId{1}, DeviceId{}, clock.now()));

    EXPECT_EQ(registry.find(ConnectionId{1})->state(), Agent::State::handshaking);
    EXPECT_EQ(registry.find(DeviceId{}), nullptr); // 롤백으로 역색인 잔류물 없음
}

TEST(AgentRegistryTest, BindRejectsBoundAgentRebind) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));
    ASSERT_TRUE(registry.bind(ConnectionId{1}, make_device_id(0xAA), clock.now()));

    EXPECT_FALSE(registry.bind(ConnectionId{1}, make_device_id(0xBB), clock.now()));

    EXPECT_EQ(registry.find(make_device_id(0xBB)), nullptr); // 롤백
    EXPECT_EQ(registry.find(ConnectionId{1})->device(), make_device_id(0xAA));
}

TEST(AgentRegistryTest, EraseConfirmingAgentClearsIndex) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));
    ASSERT_TRUE(registry.bind(ConnectionId{1}, make_device_id(0xAA), clock.now()));

    EXPECT_TRUE(registry.erase(ConnectionId{1})); // ack 전 이탈도 역색인을 비워야 한다.

    EXPECT_EQ(registry.find(ConnectionId{1}), nullptr);
    EXPECT_EQ(registry.find(make_device_id(0xAA)), nullptr);
    EXPECT_EQ(registry.size(), 0u);
}

TEST(AgentRegistryTest, EraseActiveAgentClearsIndex) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));
    ASSERT_TRUE(registry.bind(ConnectionId{1}, make_device_id(0xAA), clock.now()));
    ASSERT_TRUE(registry.find(ConnectionId{1})->confirm(clock.now()));

    EXPECT_TRUE(registry.erase(ConnectionId{1}));

    EXPECT_EQ(registry.find(ConnectionId{1}), nullptr);
    EXPECT_EQ(registry.find(make_device_id(0xAA)), nullptr);
    EXPECT_EQ(registry.size(), 0u);
}

TEST(AgentRegistryTest, EraseHandshakingAgent) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));

    EXPECT_TRUE(registry.erase(ConnectionId{1}));
    EXPECT_EQ(registry.find(ConnectionId{1}), nullptr);
}

TEST(AgentRegistryTest, EraseUnknownReturnsFalse) {
    AgentRegistry registry;

    EXPECT_FALSE(registry.erase(ConnectionId{9}));
}

TEST(AgentRegistryTest, KickThenRebindFlow) {
    // kick-old 시퀀스: 옛 연결 erase가 끝난 뒤 같은 device로 새 연결이 bind된다.
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));
    ASSERT_TRUE(registry.bind(ConnectionId{1}, make_device_id(0xAA), clock.now()));
    ASSERT_TRUE(registry.add(ConnectionId{2}, clock.now()));

    Agent* old_agent = registry.find(make_device_id(0xAA));
    ASSERT_NE(old_agent, nullptr);
    EXPECT_TRUE(registry.erase(old_agent->conn())); // disconnect 후 on_disconnected 경로의 등가물

    ASSERT_TRUE(registry.bind(ConnectionId{2}, make_device_id(0xAA), clock.now()));
    EXPECT_EQ(registry.find(make_device_id(0xAA))->conn(), ConnectionId{2});
}

TEST(AgentRegistryTest, ForEachVisitsAllAgents) {
    ManualClock clock;
    AgentRegistry registry;
    ASSERT_TRUE(registry.add(ConnectionId{1}, clock.now()));
    ASSERT_TRUE(registry.add(ConnectionId{2}, clock.now()));
    ASSERT_TRUE(registry.bind(ConnectionId{2}, make_device_id(0xAA), clock.now()));
    ASSERT_TRUE(registry.find(ConnectionId{2})->confirm(clock.now()));

    std::size_t total = 0;
    std::size_t active = 0;
    registry.for_each([&](Agent const& agent) {
        ++total;
        if (agent.state() == Agent::State::active) {
            ++active;
        }
    });

    EXPECT_EQ(total, 2u);
    EXPECT_EQ(active, 1u);
}
