#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/agent/register_service.hpp"
#include "ddcs/ctrl/app/agent/status_service.hpp"
#include "ddcs/ctrl/app/session/liveness_monitor.hpp"
#include "ddcs/ctrl/app/session/session_manager.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"
#include "ddcs/proto/msg/message.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>

#include <cstddef>
#include <cstdint>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::app::agent::RegisterService;
using ddcs::ctrl::app::agent::StatusService;
using ddcs::ctrl::app::metrics::MetricsService;
using ddcs::ctrl::app::session::LivenessMonitor;
using ddcs::ctrl::app::session::SessionManager;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;
namespace msg = ddcs::proto::msg;

class MockOutbound : public Outbound {
public:
    ddcs::common::ObjectPool<LinearBuffer> pool{ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{256})};
    PoolHandle<LinearBuffer> send_buffer() override { return pool.acquire(); }
    void send(ConnectionId, std::uint8_t, PoolHandle<LinearBuffer>) override {}
    void drop(ConnectionId) override {}
};

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return Uuid{b};
}

PoolHandle<LinearBuffer> make_outcome_body(std::uint64_t command_id) {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{64});
    auto buf = pool.acquire();
    msg::CommandOutcome const out{.command_id = command_id, .result = msg::CommandResult::success, .reason = {}};
    EXPECT_TRUE(msg::encode(out, *buf));
    return buf;
}

bool contains(std::string const& s, char const* sub) { return s.find(sub) != std::string::npos; }

struct Fixture {
    SessionRegistry sessions;
    DeviceRegistry registry;
    MockOutbound outbound;
    ManualClock clock;
    CommandService commands{sessions, outbound, clock, std::chrono::seconds{5}};
    RegisterService registrar{registry, outbound};
    StatusService status{sessions, registry};
    SessionManager mgr{sessions, registrar, status, commands, outbound, clock};
    LivenessMonitor liveness{sessions, outbound, clock, std::chrono::seconds{3}};
    MetricsService metrics{sessions, registry, commands, mgr, liveness};
};

} // namespace

TEST(MetricsServiceTest, ScrapeReportsGaugesAndAlarmCounters) {
    Fixture f;
    f.sessions.open(ConnectionId{1});
    f.sessions.open(ConnectionId{2});
    f.registry.find_or_create(make_uuid(1));

    auto const text = f.metrics.scrape();
    EXPECT_TRUE(contains(text, "# TYPE ddcs_sessions gauge"));
    EXPECT_TRUE(contains(text, "ddcs_sessions 2"));
    EXPECT_TRUE(contains(text, "ddcs_agents_registered 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_pending 0"));
    // 알람 counter 노출(미발생 -> 0).
    EXPECT_TRUE(contains(text, "# TYPE ddcs_agents_evicted_total counter"));
    EXPECT_TRUE(contains(text, "ddcs_agents_evicted_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_agents_kicked_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_commands_retried_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_commands_gave_up_total 0"));
}

TEST(MetricsServiceTest, ScrapeReportsCommandCounters) {
    Fixture f;
    auto const id = f.registry.find_or_create(make_uuid(1)).id;
    f.sessions.open(ConnectionId{1});
    f.sessions.bind(ConnectionId{1}, id, {});

    auto const cmd = f.commands.dispatch(id, 0x01, "p");
    f.clock.advance(std::chrono::milliseconds{100});
    f.commands.handle_outcome(ConnectionId{1}, make_outcome_body(cmd));

    auto const text = f.metrics.scrape();
    EXPECT_TRUE(contains(text, "# TYPE ddcs_commands_dispatched_total counter"));
    EXPECT_TRUE(contains(text, "ddcs_commands_dispatched_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_completed_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_timed_out_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_sum 100"));
}

TEST(MetricsServiceTest, ScrapeReflectsEvictionAlarm) {
    Fixture f;
    auto const id = f.registry.find_or_create(make_uuid(1)).id;
    f.sessions.open(ConnectionId{1});
    f.sessions.bind(ConnectionId{1}, id, f.clock.now()); // active, last_seen = now
    f.clock.advance(std::chrono::seconds{4});            // > 3s 침묵
    f.liveness.sweep();

    auto const text = f.metrics.scrape();
    EXPECT_TRUE(contains(text, "ddcs_agents_evicted_total 1"));
}
