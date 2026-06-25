#include "ddcs/ctrl/app/session/device_roster.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::ctrl::app::session::DeviceRoster;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::transport::port::ConnectionId;
using ddcs::ctrl::domain::DeviceId;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

TEST(DeviceRosterTest, YieldsOnlyActiveDevices) {
    ManualClock clock;
    SessionRegistry sessions;
    DeviceRoster roster{sessions};

    ASSERT_TRUE(sessions.add(ConnectionId{1}, clock.now())); // handshaking
    ASSERT_TRUE(sessions.add(ConnectionId{2}, clock.now()));
    ASSERT_TRUE(sessions.bind(ConnectionId{2}, make_device_id(0xBB), clock.now())); // confirming
    ASSERT_TRUE(sessions.add(ConnectionId{3}, clock.now()));
    ASSERT_TRUE(sessions.bind(ConnectionId{3}, make_device_id(0xCC), clock.now()));
    ASSERT_TRUE(sessions.find(ConnectionId{3})->confirm(clock.now())); // active

    std::vector<DeviceId> seen;
    roster.for_each_active([&](DeviceId id) { seen.push_back(id); });

    ASSERT_EQ(seen.size(), 1u); // 등록 미완 연결은 명령 대상이 아니다
    EXPECT_EQ(seen[0], make_device_id(0xCC));
}

TEST(DeviceRosterTest, YieldsNothingWhenEmpty) {
    SessionRegistry sessions;
    DeviceRoster roster{sessions};

    std::size_t count = 0;
    roster.for_each_active([&](DeviceId) { ++count; });

    EXPECT_EQ(count, 0u);
}

} // namespace
