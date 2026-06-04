#include "ddcs/ctrl/app/session/liveness_monitor.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <utility>
#include <vector>

#include <cstdint>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::PoolHandle;
using ddcs::ctrl::app::session::LivenessMonitor;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::session::State;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::port::transport::CloseMode;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;

class MockOutbound : public Outbound {
public:
    std::vector<std::pair<ConnectionId, CloseMode>> closes;
    PoolHandle<LinearBuffer> payload_buffer() override { return {}; }
    void send(ConnectionId, std::uint8_t, PoolHandle<LinearBuffer>) override {}
    void close(ConnectionId id, CloseMode mode) override { closes.emplace_back(id, mode); }
};

struct Fixture {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    LivenessMonitor mon{sessions, outbound, clock, std::chrono::seconds{3}};

    void make_active(ConnectionId conn, DeviceId agent) {
        sessions.open(conn);
        sessions.bind(conn, agent, clock.now()); // active, last_seen = now
    }
};

} // namespace

TEST(LivenessMonitorTest, KeepsFreshActiveSession) {
    Fixture f;
    f.make_active(ConnectionId{1}, DeviceId{1});
    f.clock.advance(std::chrono::seconds{2}); // < 3s
    f.mon.sweep();
    EXPECT_TRUE(f.outbound.closes.empty());
    EXPECT_EQ(f.mon.evicted_total(), 0u);
}

TEST(LivenessMonitorTest, EvictsSilentActiveSession) {
    Fixture f;
    f.make_active(ConnectionId{1}, DeviceId{1});
    f.clock.advance(std::chrono::seconds{4}); // > 3s 침묵
    f.mon.sweep();
    ASSERT_EQ(f.outbound.closes.size(), 1u);
    EXPECT_EQ(f.outbound.closes[0].first, ConnectionId{1});
    EXPECT_EQ(f.outbound.closes[0].second, CloseMode::force);
    EXPECT_EQ(f.mon.evicted_total(), 1u); // 알람 counter
}

TEST(LivenessMonitorTest, IgnoresHandshakingSession) {
    Fixture f;
    f.sessions.open(ConnectionId{1}); // handshaking (미등록)
    f.clock.advance(std::chrono::seconds{10});
    f.mon.sweep();
    EXPECT_TRUE(f.outbound.closes.empty()); // handshaking 은 coordinator handshake 타이머 소관
}

TEST(LivenessMonitorTest, IgnoresClosingSession) {
    Fixture f;
    f.make_active(ConnectionId{1}, DeviceId{1});
    f.sessions.find(ConnectionId{1})->state = State::closing; // 드레인 중
    f.clock.advance(std::chrono::seconds{10});
    f.mon.sweep();
    EXPECT_TRUE(f.outbound.closes.empty()); // closing 은 coordinator pw 소관
}

TEST(LivenessMonitorTest, FreshenedSessionSurvives) {
    Fixture f;
    f.make_active(ConnectionId{1}, DeviceId{1});
    f.clock.advance(std::chrono::seconds{2});
    f.sessions.find(ConnectionId{1})->update_seen(f.clock.now()); // 트래픽 도착 -> 갱신
    f.clock.advance(std::chrono::seconds{2});                     // 마지막 관측 후 2s
    f.mon.sweep();
    EXPECT_TRUE(f.outbound.closes.empty());
}
