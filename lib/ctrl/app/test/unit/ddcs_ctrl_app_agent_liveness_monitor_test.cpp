#include "ddcs/ctrl/app/agent/liveness_monitor.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/ctrl/app/agent/agent_registry.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/disconnector.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::ctrl::app::agent::AgentRegistry;
using ddcs::ctrl::app::agent::LivenessMonitor;
using ddcs::ctrl::app::agent::port::ConnectionId;
using ddcs::ctrl::app::agent::port::Disconnector;
using ddcs::ctrl::domain::DeviceId;
using namespace std::chrono_literals;

// infra 계약처럼 disconnect가 동기로 erase까지 끝내는 대역.
class FakeDisconnector final : public Disconnector {
public:
    explicit FakeDisconnector(AgentRegistry& registry) noexcept : registry_{registry} {}

    std::vector<ConnectionId> disconnected;

    void disconnect(ConnectionId id) override {
        disconnected.push_back(id);
        registry_.erase(id);
    }

private:
    AgentRegistry& registry_;
};

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

struct MonitorFixture {
    ManualClock clock;
    AgentRegistry registry;
    FakeDisconnector disconnector{registry};
    LivenessMonitor monitor{registry, disconnector, 3s};

    ConnectionId activate(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        EXPECT_TRUE(registry.add(id, clock.now()));
        EXPECT_TRUE(registry.bind(id, make_device_id(seed), clock.now()));
        EXPECT_TRUE(registry.find(id)->confirm(clock.now()));
        return id;
    }
};

} // namespace

TEST(AgentLivenessMonitorTest, SilentActiveEvicted) {
    MonitorFixture f;
    ConnectionId const id = f.activate(1, 0xAA);

    f.clock.advance(4s);
    f.monitor.sweep(f.clock.now());

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.disconnector.disconnected[0], id);
    EXPECT_EQ(f.registry.find(id), nullptr);
    EXPECT_EQ(f.monitor.evicted_total(), 1u);
}

TEST(AgentLivenessMonitorTest, RecentlySeenActiveSurvives) {
    MonitorFixture f;
    ConnectionId const id = f.activate(1, 0xAA);

    f.clock.advance(2s);
    f.registry.find(id)->update_seen(f.clock.now()); // 활동 관측
    f.clock.advance(2s);
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty()); // 누적 4s지만 마지막 활동 기준 2s
}

TEST(AgentLivenessMonitorTest, ExactTimeoutBoundarySurvives) {
    MonitorFixture f;
    f.activate(1, 0xAA);

    f.clock.advance(3s); // 경계: now - last_seen == timeout
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty());
}

TEST(AgentLivenessMonitorTest, HandshakingAgentIgnored) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));

    f.clock.advance(10s);
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty()); // handshaking 시한은 HandshakeMonitor 소관
}

TEST(AgentLivenessMonitorTest, ConfirmingAgentIgnored) {
    MonitorFixture f;
    ASSERT_TRUE(f.registry.add(ConnectionId{1}, f.clock.now()));
    ASSERT_TRUE(f.registry.bind(ConnectionId{1}, make_device_id(0xAA), f.clock.now()));

    f.clock.advance(10s);
    f.monitor.sweep(f.clock.now());

    EXPECT_TRUE(f.disconnector.disconnected.empty()); // ack 대기 시한도 HandshakeMonitor 소관
}

TEST(AgentLivenessMonitorTest, SweepSurvivesSynchronousErase) {
    // 처형(disconnect)이 동기로 registry를 줄여도 수집 단계 덕에 순회가 안전해야 한다.
    MonitorFixture f;
    f.activate(1, 0xAA);
    f.activate(2, 0xBB);
    f.activate(3, 0xCC);

    f.clock.advance(4s);
    f.monitor.sweep(f.clock.now());

    EXPECT_EQ(f.disconnector.disconnected.size(), 3u);
    EXPECT_EQ(f.registry.size(), 0u);
    EXPECT_EQ(f.monitor.evicted_total(), 3u);
}
