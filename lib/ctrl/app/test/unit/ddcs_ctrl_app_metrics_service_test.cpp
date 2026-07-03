#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/command.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/app/session/device_roster.hpp"
#include "ddcs/ctrl/app/session/handshake_monitor.hpp"
#include "ddcs/ctrl/app/session/liveness_monitor.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::common::Uuid;
using ddcs::ctrl::app::device::CommandService;
using ddcs::ctrl::app::device::port::Command;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::CommandSender;
using ddcs::ctrl::app::device::port::SetMode;
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

bool contains(std::string const& s, char const* sub) {
    return s.find(sub) != std::string::npos;
}

class FakeCommandSender final : public CommandSender {
public:
    bool accept = true;
    bool try_send(ddcs::ctrl::domain::DeviceId, CommandId, Command const&) override {
        return accept;
    }
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
    ddcs::ctrl::app::session::DeviceRoster roster{sessions};
    ddcs::ctrl::domain::GroupPolicy policy;
    ddcs::ctrl::app::metrics::SweepStats sweep;
    MetricsService metrics{sessions, devices, roster, commands, liveness, handshake, policy, sweep};

    ddcs::ctrl::domain::DeviceId activate(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        EXPECT_TRUE(sessions.add(id, clock.now()));
        EXPECT_TRUE(sessions.bind(id, make_uuid(seed), clock.now()));
        EXPECT_TRUE(sessions.find(id)->confirm(clock.now()));
        return make_uuid(seed);
    }

    CommandId send(ddcs::ctrl::domain::DeviceId device) {
        return commands.dispatch(
            device, SetMode{.mode = ddcs::device::Mode::performance}, clock.now()
        );
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

TEST(MetricsServiceTest, ScrapeReportsSweepDuration) {
    Fixture f;
    f.sweep.record(std::chrono::microseconds{1500});
    f.sweep.record(std::chrono::microseconds{500});

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_sweep_duration_us gauge"));
    EXPECT_TRUE(contains(text, "ddcs_sweep_duration_us 500"));      // 직전 tick
    EXPECT_TRUE(contains(text, "ddcs_sweep_duration_us_max 1500")); // 시작 후 최대
    EXPECT_TRUE(contains(text, "ddcs_sweep_duration_us_sum 2000")); // 1500+500
    EXPECT_TRUE(contains(text, "ddcs_sweep_ticks_total 2"));
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
    EXPECT_TRUE(contains(text, "# TYPE ddcs_command_rtt_ms histogram"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"50\"} 0"));  // 100ms > 50
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"100\"} 1")); // 100ms <= 100
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"+Inf\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_count 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_superseded_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_commands_stale_total 0"));
}

TEST(MetricsServiceTest, ScrapeRttHistogramCumulates) {
    Fixture f;
    auto const device = f.activate(1, 0xAA);
    // 서로 다른 버킷에 떨어지는 3개 완료: 5ms / 30ms / 200ms
    for (int ms : {5, 30, 200}) {
        auto const id = f.send(device);
        f.clock.advance(std::chrono::milliseconds{ms});
        f.commands.settle(device, id, true, "", f.clock.now());
    }

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"5\"} 1"));   // 5ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"20\"} 1"));  // 여전히 5ms만
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"50\"} 2"));  // +30ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"250\"} 3")); // +200ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_bucket{le=\"+Inf\"} 3"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_count 3"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_ms_sum 235")); // 5+30+200
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

TEST(MetricsServiceTest, ScrapeReportsGroupGauges) {
    using ddcs::ctrl::domain::GroupRule;
    using ddcs::ctrl::domain::Status;
    using ddcs::device::Mode;
    Fixture f;
    // 메트릭은 정책 group으로 한정되므로 zone_a/zone_b를 정책에 등록
    f.policy.set("zone_a", *GroupRule::try_make(70, 30, Mode::performance, Mode::normal));
    f.policy.set("zone_b", *GroupRule::try_make(60, 45, Mode::performance, Mode::safe));

    // zone_a: 2개 active (load 80, 60 -> avg 70), 둘 다 performance
    auto const a1 = f.activate(1, 0x01);
    auto const a2 = f.activate(2, 0x02);
    f.devices.find_or_create(a1);
    f.devices.set_group(a1, "zone_a");
    f.devices.update_status(a1, Status{.mode = Mode::performance, .load = 80.0, .temp = 50.0});
    f.devices.find_or_create(a2);
    f.devices.set_group(a2, "zone_a");
    f.devices.update_status(a2, Status{.mode = Mode::performance, .load = 60.0, .temp = 60.0});
    // zone_b: 1개 active (load 10), safe
    auto const b1 = f.activate(3, 0x03);
    f.devices.find_or_create(b1);
    f.devices.set_group(b1, "zone_b");
    f.devices.update_status(b1, Status{.mode = Mode::safe, .load = 10.0, .temp = 30.0});

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_group_load_avg gauge"));
    EXPECT_TRUE(contains(text, "ddcs_group_load_avg{group=\"zone_a\"} 70")); // (80+60)/2
    EXPECT_TRUE(contains(text, "ddcs_group_load_avg{group=\"zone_b\"} 10"));
    EXPECT_TRUE(contains(text, "ddcs_group_temp_avg{group=\"zone_a\"} 55")); // (50+60)/2
    EXPECT_TRUE(contains(text, "ddcs_group_temp_avg{group=\"zone_b\"} 30"));
    EXPECT_TRUE(contains(text, "ddcs_group_devices{group=\"zone_a\",mode=\"performance\"} 2"));
    EXPECT_TRUE(contains(text, "ddcs_group_devices{group=\"zone_a\",mode=\"safe\"} 0"));
    EXPECT_TRUE(contains(text, "ddcs_group_devices{group=\"zone_b\",mode=\"safe\"} 1"));
}

} // namespace
