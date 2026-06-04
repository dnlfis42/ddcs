#include "ddcs/ctrl/app/session/session_registry.hpp"

#include "ddcs/ctrl/app/session/session.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::session::State;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::port::transport::ConnectionId;

} // namespace

TEST(SessionRegistryTest, OpenCreatesHandshakingSession) {
    SessionRegistry reg;
    auto& s = reg.open(ConnectionId{1});
    EXPECT_EQ(s.state, State::handshaking);
    EXPECT_FALSE(s.agent.valid());
    EXPECT_EQ(reg.size(), 1u);
}

TEST(SessionRegistryTest, BindActivatesAndResolves) {
    SessionRegistry reg;
    reg.open(ConnectionId{1});
    ConnectionId const kicked = reg.bind(ConnectionId{1}, DeviceId{42}, {});
    EXPECT_FALSE(kicked.valid()); // 첫 바인딩 -> kick 없음

    auto* s = reg.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, State::active);
    EXPECT_EQ(s->agent, DeviceId{42});
    EXPECT_EQ(reg.resolve(DeviceId{42}), ConnectionId{1});
}

TEST(SessionRegistryTest, BindKicksOldConnectionForSameAgent) {
    SessionRegistry reg;
    reg.open(ConnectionId{1});
    reg.bind(ConnectionId{1}, DeviceId{7}, {});
    reg.open(ConnectionId{2});
    ConnectionId const kicked = reg.bind(ConnectionId{2}, DeviceId{7}, {}); // 같은 agent, 새 conn

    EXPECT_EQ(kicked, ConnectionId{1});                   // 옛 conn 반환(kick 대상)
    EXPECT_EQ(reg.resolve(DeviceId{7}), ConnectionId{2}); // 현재 바인딩 = 새 conn
}

TEST(SessionRegistryTest, RebindSameConnDoesNotKick) {
    SessionRegistry reg;
    reg.open(ConnectionId{1});
    reg.bind(ConnectionId{1}, DeviceId{3}, {});
    ConnectionId const kicked = reg.bind(ConnectionId{1}, DeviceId{3}, {}); // 같은 conn 재바인딩
    EXPECT_FALSE(kicked.valid());
    EXPECT_EQ(reg.resolve(DeviceId{3}), ConnectionId{1});
}

TEST(SessionRegistryTest, EraseRemovesSessionAndReverse) {
    SessionRegistry reg;
    reg.open(ConnectionId{1});
    reg.bind(ConnectionId{1}, DeviceId{5}, {});
    reg.erase(ConnectionId{1});
    EXPECT_EQ(reg.size(), 0u);
    EXPECT_FALSE(reg.resolve(DeviceId{5}).valid()); // reverse 정리됨
    EXPECT_EQ(reg.find(ConnectionId{1}), nullptr);
}

TEST(SessionRegistryTest, EraseKickedOldConnPreservesReverse) {
    SessionRegistry reg;
    reg.open(ConnectionId{1});
    reg.bind(ConnectionId{1}, DeviceId{9}, {});
    reg.open(ConnectionId{2});
    reg.bind(ConnectionId{2}, DeviceId{9}, {}); // kick conn1 -> 현재 = conn2
    reg.erase(ConnectionId{1});                 // 옛 conn disconnect

    EXPECT_EQ(reg.resolve(DeviceId{9}), ConnectionId{2}); // reverse 는 conn2 가 가졌으므로 보존
    EXPECT_NE(reg.find(ConnectionId{2}), nullptr);
}

TEST(SessionRegistryTest, EraseHandshakingSessionIsSafe) {
    SessionRegistry reg;
    reg.open(ConnectionId{3}); // handshaking, agent 없음
    reg.erase(ConnectionId{3});
    EXPECT_EQ(reg.size(), 0u);
}
