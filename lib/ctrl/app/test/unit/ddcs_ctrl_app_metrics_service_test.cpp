#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/command_buffer.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/app/session/handshake_monitor.hpp"
#include "ddcs/ctrl/app/session/liveness_monitor.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::ObjectPool;
using ddcs::common::Uuid;
using ddcs::ctrl::app::device::CommandService;
using ddcs::ctrl::app::device::port::CommandBuffer;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::CommandSender;
using ddcs::ctrl::app::metrics::MetricsService;
using ddcs::ctrl::app::session::HandshakeMonitor;
using ddcs::ctrl::app::session::LivenessMonitor;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::transport::port::ConnectionId;
using ddcs::ctrl::app::transport::port::Disconnector;
using ddcs::ctrl::domain::DeviceRegistry;
using namespace std::chrono_literals;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b[0] = std::byte{seed};
    return Uuid{b};
}

std::span<std::byte const> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

bool contains(std::string const& s, char const* sub) {
    return s.find(sub) != std::string::npos;
}

class FakeCommandSender final : public CommandSender {
public:
    bool accept = true;
    CommandBuffer make_command_buffer() override {
        return pool_.acquire();
    }
    bool try_send(ddcs::ctrl::domain::DeviceId, CommandId, std::uint8_t, CommandBuffer) override {
        return accept;
    }

private:
    ObjectPool<LinearBuffer> pool_{
        ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{64})
    };
};

// infra처럼 disconnect가 동기로 registry erase까지 끝내는 대역
class FakeDisconnector final : public Disconnector {
public:
    explicit FakeDisconnector(SessionRegistry& sessions) noexcept
        : sessions_{sessions} {}
    void disconnect(ConnectionId id) override {
        sessions_.erase(id);
    }

private:
    SessionRegistry& sessions_;
};

struct Fixture {
    ManualClock clock;
    SessionRegistry sessions;
    DeviceRegistry devices;
    FakeCommandSender sender;
    FakeDisconnector disconnector{sessions};
    CommandService commands{sender, 5s, 1, 500ms};
    HandshakeMonitor handshake{sessions, disconnector, 3s};
    LivenessMonitor liveness{sessions, disconnector, 3s};
    MetricsService metrics{sessions, devices, commands, liveness, handshake};

    ddcs::ctrl::domain::DeviceId activate(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        EXPECT_TRUE(sessions.add(id, clock.now()));
        EXPECT_TRUE(sessions.bind(id, make_uuid(seed), clock.now()));
        EXPECT_TRUE(sessions.find(id)->confirm(clock.now()));
        return make_uuid(seed);
    }

    CommandId send(ddcs::ctrl::domain::DeviceId device) {
        auto buf = commands.make_command_buffer();
        EXPECT_TRUE(buf->try_append(as_bytes("p")));
        return commands.dispatch(device, 0x01, std::move(buf), clock.now());
    }
};

TEST(MetricsServiceTest, ScrapeReportsGauges) {
    Fixture f;
    EXPECT_TRUE(
        f.sessions.add(ConnectionId{1}, f.clock.now())
    ); // handshaking도 connection으로 집계
    EXPECT_TRUE(f.sessions.add(ConnectionId{2}, f.clock.now()));
    f.devices.find_or_create(make_uuid(1));

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_connections gauge"));
    EXPECT_TRUE(contains(text, "ddcs_connections 2"));
    EXPECT_TRUE(contains(text, "ddcs_devices_known 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_pending 0"));
}

TEST(MetricsServiceTest, ScrapeReportsCommandCounters) {
    Fixture f;
    auto const device = f.activate(1, 0xAA);

    auto const id = f.send(device);
    f.clock.advance(100ms);
    f.commands.settle(device, id, true, "", f.clock.now());

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_commands_dispatched_total counter"));
    EXPECT_TRUE(contains(text, "ddcs_commands_dispatched_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_completed_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_timed_out_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_sum 100"));
    EXPECT_TRUE(contains(text, "ddcs_commands_superseded_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_commands_stale_total 0"));
}

TEST(MetricsServiceTest, ScrapeReportsSupersedeAndStale) {
    Fixture f;
    auto const device = f.activate(1, 0xAA);

    auto const first = f.send(device);
    f.send(device);                                            // 같은 device+type이라 supersede
    f.commands.settle(device, first, true, "", f.clock.now()); // 대체된 id라서 stale

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "ddcs_commands_superseded_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_stale_total 1"));
}

TEST(MetricsServiceTest, ScrapeReflectsEvictionAlarm) {
    Fixture f;
    f.activate(1, 0xAA);

    f.clock.advance(4s); // > liveness 3s 침묵
    f.liveness.sweep(f.clock.now());

    auto const text = f.metrics.scrape();
    EXPECT_TRUE(contains(text, "ddcs_agents_evicted_total 1"));
}

TEST(MetricsServiceTest, ScrapeReflectsHandshakeExpiry) {
    Fixture f;
    EXPECT_TRUE(f.sessions.add(ConnectionId{1}, f.clock.now())); // handshaking, 등록 미완

    f.clock.advance(4s); // > handshake 3s
    f.handshake.sweep(f.clock.now());

    auto const text = f.metrics.scrape();
    EXPECT_TRUE(contains(text, "ddcs_handshake_expired_total 1"));
}

} // namespace
