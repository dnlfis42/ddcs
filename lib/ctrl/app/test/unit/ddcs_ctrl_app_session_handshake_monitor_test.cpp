#include "ddcs/ctrl/app/session/handshake_monitor.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::ctrl::app::session::HandshakeMonitor;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::transport::port::ConnectionId;
using ddcs::ctrl::app::transport::port::Disconnector;
using ddcs::ctrl::domain::DeviceId;
using namespace std::chrono_literals;

// infra 계약처럼 disconnect가 동기로 erase까지 끝내는 대역
class FakeDisconnector final : public Disconnector {
public:
    explicit FakeDisconnector(SessionRegistry& registry) noexcept
        : registry_(registry) {}

    std::vector<ConnectionId> disconnected;

    void disconnect(ConnectionId id) override {
        disconnected.push_back(id);
        registry_.erase(id);
    }

private:
    SessionRegistry& registry_;
};

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

struct MonitorFixture {
    ManualClock clock;
    SessionRegistry registry;
    FakeDisconnector disconnector{registry};
    HandshakeMonitor monitor{registry, disconnector, 3s};
};

TEST(HandshakeMonitorTest, ExpiredHandshakingDisconnected) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));

    f.clock.advance(4s);
    f.monitor.sweep(f.clock.now());

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.disconnector.disconnected[0], ConnectionId{1});
    EXPECT_EQ(f.registry.find(ConnectionId{1}), nullptr);
    EXPECT_EQ(f.monitor.expired_total(), 1u);
}

TEST(HandshakeMonitorTest, FreshHandshakingSurvives) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));

    f.clock.advance(2s);
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty());
    EXPECT_NE(f.registry.find(ConnectionId{1}), nullptr);
}

TEST(HandshakeMonitorTest, ExactTimeoutBoundarySurvives) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));

    f.clock.advance(3s); // 경계: now - last_seen == timeout
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty());
}

TEST(HandshakeMonitorTest, ExpiredConfirmingDisconnected) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));
    ASSERT_TRUE(f.registry.bind(ConnectionId{1}, make_device_id(0xAA), f.clock.now()));

    f.clock.advance(4s); // RegisterAck 미수신
    f.monitor.sweep(f.clock.now());

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.monitor.expired_total(), 1u);
}

TEST(HandshakeMonitorTest, BindGrantsNewBudgetForAck) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));

    f.clock.advance(2s); // 접속 후 2s 뒤 등록 요청 도착
    ASSERT_TRUE(f.registry.bind(ConnectionId{1}, make_device_id(0xAA), f.clock.now()));

    f.clock.advance(2s); // 접속 기준 4s지만 bind 기준 2s
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty()); // ack 대기 구간은 자기 budget을 받는다.
}

TEST(HandshakeMonitorTest, ActiveAgentIgnored) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));
    ASSERT_TRUE(f.registry.bind(ConnectionId{1}, make_device_id(0xAA), f.clock.now()));
    ASSERT_TRUE(f.registry.find(ConnectionId{1})->confirm(f.clock.now()));

    f.clock.advance(10s);
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty()); // active 침묵은 LivenessMonitor 소관
}

TEST(HandshakeMonitorTest, SweepSurvivesSynchronousErase) {
    // 처형(disconnect)이 동기로 registry를 줄여도 수집 단계 덕에 순회가 안전해야 한다.
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));
    ASSERT_TRUE(f.registry.add(ConnectionId{2}, f.clock.now()));
    ASSERT_TRUE(f.registry.add(ConnectionId{3}, f.clock.now()));

    f.clock.advance(4s);
    f.monitor.sweep(f.clock.now());

    EXPECT_EQ(f.disconnector.disconnected.size(), 3u);
    EXPECT_EQ(f.registry.size(), 0u);
    EXPECT_EQ(f.monitor.expired_total(), 3u);
}

} // namespace
