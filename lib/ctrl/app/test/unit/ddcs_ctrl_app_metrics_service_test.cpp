#include "ddcs/ctrl/app/metrics/metrics_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/app/device/port/device_release_sink.hpp"
#include "ddcs/ctrl/app/device/registration_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/session/session_service.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/connection_listener.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/app/transport/port/message_buffer.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"
#include "ddcs/wire/command/command.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::common::Uuid;
using ddcs::ctrl::app::device::CommandService;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::CommandSender;
using ddcs::ctrl::app::device::port::SendResult;
using ddcs::ctrl::app::metrics::MetricsService;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::session::SessionService;
using ddcs::ctrl::app::transport::port::ConnectionId;
using ddcs::ctrl::app::transport::port::Disconnector;
using ddcs::ctrl::app::transport::port::MessageBuffer;
using ddcs::ctrl::app::transport::port::MessageSender;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::wire::command::Command;
using ddcs::wire::command::SetMode;
using namespace std::chrono_literals;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b[0] = std::byte{seed};
    return Uuid{b};
}

bool contains(std::string const& s, std::string_view sub) {
    return s.find(sub) != std::string::npos;
}

class FakeCommandSender final : public CommandSender {
public:
    SendResult result = SendResult::ok;

    SendResult send(ddcs::ctrl::domain::DeviceId, CommandId, Command const&) override {
        return result;
    }
};

// 실제 전송 계층처럼 disconnect() 안에서 연결 종료 콜백을 호출한다.
class FakeDisconnector final : public Disconnector {
public:
    explicit FakeDisconnector(SessionRegistry& sessions) noexcept
        : sessions_{sessions} {}

    ddcs::ctrl::app::transport::port::ConnectionListener* listener = nullptr;

    void disconnect(ConnectionId id, ddcs::ctrl::app::transport::port::DisconnectReason reason)
        override {
        if (listener != nullptr) {
            listener->on_disconnected(id, reason);
            return;
        }
        sessions_.erase(id);
    }

private:
    SessionRegistry& sessions_;
};

// 세션 점검에 필요한 메시지 수신 대역이다. 이 테스트에서는 수신하지 않는다.
class NoopMessageSender final : public MessageSender {
public:
    MessageBuffer make_message_buffer() override {
        return {}; // 이 테스트에서는 호출하지 않는다.
    }
    void send(ConnectionId, MessageBuffer) override {}
};

class NoopReleaseSink final : public ddcs::ctrl::app::device::port::DeviceReleaseSink {
public:
    void on_device_released(ddcs::ctrl::domain::DeviceId) override {}
};

// 전송 자원 통계를 지정해 메트릭 출력값을 검증한다.
class FakeTransportStatsSource final
    : public ddcs::ctrl::app::transport::port::TransportStatsSource {
public:
    ddcs::ctrl::app::transport::port::TransportStats stats{};

    [[nodiscard]] ddcs::ctrl::app::transport::port::TransportStats
    transport_stats() const override {
        return stats;
    }
};

struct Fixture {
    ManualClock clock;
    SessionRegistry sessions;
    DeviceRegistry devices;
    FakeCommandSender sender;
    FakeDisconnector disconnector{sessions};
    CommandService commands{sender, 5s, 2, 500ms};
    NoopMessageSender outbox;
    NoopReleaseSink release_sink;
    ddcs::ctrl::app::device::RegistrationService registration{devices};
    ddcs::ctrl::app::device::StatusService status{devices};
    ddcs::ctrl::domain::GroupPolicy policy;
    SessionService session_service{sessions,     disconnector, outbox,   clock,
                                   registration, status,       commands, release_sink,
                                   policy,       3s,           3s};
    ddcs::ctrl::app::metrics::TickStats sweep;
    FakeTransportStatsSource transport;
    MetricsService metrics{sessions,        devices, sessions, commands,
                           session_service, policy,  sweep,    transport};

    Fixture() {
        disconnector.listener = &session_service;
    }

    ddcs::ctrl::domain::DeviceId activate(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        EXPECT_TRUE(sessions.add(id, clock.now()));
        EXPECT_TRUE(sessions.bind(id, make_uuid(seed), clock.now()));
        EXPECT_TRUE(sessions.find(id)->confirm(clock.now()));
        return make_uuid(seed);
    }

    CommandId send(ddcs::ctrl::domain::DeviceId device) {
        return commands.dispatch(
            device, SetMode{.mode = ddcs::device::encode_mode(ddcs::device::Mode::performance)},
            clock.now()
        );
    }
};

TEST(MetricsServiceTest, ScrapeReportsGauges) {
    Fixture f;
    EXPECT_TRUE(f.sessions.add(ConnectionId{1}, f.clock.now())
    ); // 등록 중인 연결도 집계한다.
    EXPECT_TRUE(f.sessions.add(ConnectionId{2}, f.clock.now()));
    f.devices.enroll(make_uuid(1), "");

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_connections gauge"));
    EXPECT_TRUE(contains(text, "ddcs_connections 2"));
    EXPECT_TRUE(contains(text, "ddcs_devices 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_pending 0"));
}

TEST(MetricsServiceTest, ScrapeReportsTransportStats) {
    Fixture f;
    f.transport.stats = {
        .tx_queued_messages = 7,
        .connection_pool_capacity = 64,
        .connection_pool_acquired = 3,
        .message_pool_capacity = 128,
        .message_pool_acquired = 5,
    };

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_send_queue_messages gauge"));
    EXPECT_TRUE(contains(text, "ddcs_send_queue_messages 7"));
    EXPECT_TRUE(contains(text, "# TYPE ddcs_pool_slots gauge"));
    EXPECT_TRUE(contains(text, "ddcs_pool_slots{pool=\"connection\"} 64"));
    EXPECT_TRUE(contains(text, "ddcs_pool_slots{pool=\"message\"} 128"));
    EXPECT_TRUE(contains(text, "ddcs_pool_slots_acquired{pool=\"connection\"} 3"));
    EXPECT_TRUE(contains(text, "ddcs_pool_slots_acquired{pool=\"message\"} 5"));
    // 수신 메시지가 없으므로 메시지 누적 횟수는 0이다.
    EXPECT_TRUE(contains(text, "# TYPE ddcs_messages_received_total counter"));
    EXPECT_TRUE(contains(text, "ddcs_messages_received_total 0"));
}

TEST(MetricsServiceTest, ScrapeReportsTickDuration) {
    Fixture f;
    f.sweep.work.record(std::chrono::microseconds{1500});
    f.sweep.work.record(std::chrono::microseconds{500});

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_tick_duration_seconds gauge"));
    EXPECT_TRUE(contains(text, "ddcs_tick_duration_seconds 0.0005"));      // 직전 tick
    EXPECT_TRUE(contains(text, "ddcs_tick_duration_seconds_max 0.0015"));  // 시작 후 최대
    EXPECT_TRUE(contains(text, "ddcs_tick_duration_seconds_total 0.002")); // 1500+500us
    EXPECT_TRUE(contains(text, "ddcs_ticks_total 2"));
}

TEST(MetricsServiceTest, ScrapeSeparatesSchedulingFromCompletedWork) {
    Fixture f;
    auto const initial = f.metrics.scrape();
    EXPECT_TRUE(contains(initial, "ddcs_tick_start_lateness_seconds 0\n"));
    EXPECT_TRUE(contains(initial, "ddcs_tick_start_lateness_seconds_max 0\n"));
    EXPECT_TRUE(contains(initial, "ddcs_tick_skipped_total 0\n"));

    f.sweep.start_lateness.record(200ms);
    f.sweep.start_lateness.record(15ms);
    f.sweep.skipped_total = 4;
    auto const text = f.metrics.scrape();
    EXPECT_TRUE(contains(text, "# TYPE ddcs_tick_start_lateness_seconds gauge"));
    EXPECT_TRUE(contains(text, "ddcs_tick_start_lateness_seconds 0.015\n"));
    EXPECT_TRUE(contains(text, "# TYPE ddcs_tick_start_lateness_seconds_max gauge"));
    EXPECT_TRUE(contains(text, "ddcs_tick_start_lateness_seconds_max 0.2\n"));
    EXPECT_TRUE(contains(text, "# TYPE ddcs_tick_skipped_total counter"));
    EXPECT_TRUE(contains(text, "ddcs_tick_skipped_total 4\n"));
    EXPECT_TRUE(contains(text, "ddcs_ticks_total 0\n"));
    EXPECT_TRUE(contains(text, "ddcs_tick_duration_seconds_total 0\n"));
}

TEST(MetricsServiceTest, ScrapeReportsCommandCounters) {
    Fixture f;
    auto const device = f.activate(1, 0xAA);

    auto const id = f.send(device);
    f.clock.advance(100ms);
    f.commands.settle(device, id, true, 0, f.clock.now());

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_commands_dispatched_total counter"));
    EXPECT_TRUE(contains(text, "ddcs_commands_dispatched_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_succeeded_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_failed_total{reason=\"exhausted\"} 0"));
    EXPECT_TRUE(contains(text, "ddcs_command_attempt_failures_total{reason=\"timeout\"} 0"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_sum 0.1"));
    EXPECT_TRUE(contains(text, "# TYPE ddcs_command_rtt_seconds histogram"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"0.05\"} 0")); // 100ms > 50ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"0.1\"} 1"));  // 100ms <= 100ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"+Inf\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_count 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_superseded_total 0"));
    EXPECT_TRUE(contains(text, "ddcs_command_stale_responses_total 0"));
    EXPECT_FALSE(contains(text, "ddcs_commands_completed_total")); // 이전 메트릭 이름은 출력하지 않는다.
}

TEST(MetricsServiceTest, ScrapeSeparatesCommandFailureFamilies) {
    Fixture f;
    auto const device = f.activate(1, 0xAA);

    // 첫 전송에 실패한 명령은 전송된 명령 수에 포함하지 않는다.
    f.sender.result = SendResult::offline;
    EXPECT_FALSE(f.send(device).valid());
    f.sender.result = SendResult::encode_fail;
    EXPECT_FALSE(f.send(device).valid());

    // Agent가 명령을 거부한 뒤 재전송하지만, 응답 기한이 지나 최대 시도 횟수에 도달한다.
    f.sender.result = SendResult::ok;
    auto const rejected = f.send(device);
    ASSERT_TRUE(rejected.valid());
    f.commands.settle(device, rejected, false, 2, f.clock.now());
    f.clock.advance(600ms);
    f.commands.sweep(f.clock.now()); // 재전송 성공
    f.clock.advance(6s);
    f.commands.sweep(f.clock.now()); // 두 번째 시도의 응답 기한이 지나 최종 실패한다.

    // 재전송 자체에 실패하면 해당 명령은 최종 실패로 처리한다.
    auto const offline = f.send(device);
    ASSERT_TRUE(offline.valid());
    f.clock.advance(6s);
    f.commands.sweep(f.clock.now());
    f.sender.result = SendResult::offline;
    f.clock.advance(600ms);
    f.commands.sweep(f.clock.now());

    f.sender.result = SendResult::ok;
    auto const encode_failure = f.send(device);
    ASSERT_TRUE(encode_failure.valid());
    f.clock.advance(6s);
    f.commands.sweep(f.clock.now());
    f.sender.result = SendResult::encode_fail;
    f.clock.advance(600ms);
    f.commands.sweep(f.clock.now());

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "ddcs_commands_dispatched_total 3"));
    EXPECT_TRUE(contains(text, "ddcs_commands_failed_total{reason=\"exhausted\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_failed_total{reason=\"offline\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_failed_total{reason=\"encode_fail\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_command_dispatch_failures_total{reason=\"offline\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_command_dispatch_failures_total{reason=\"encode_fail\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_command_attempt_failures_total{reason=\"agent_failure\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_command_attempt_failures_total{reason=\"timeout\"} 3"));
    EXPECT_TRUE(contains(text, "ddcs_command_resends_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_commands_pending 0"));
}

TEST(MetricsServiceTest, ScrapeRttHistogramCumulates) {
    Fixture f;
    auto const device = f.activate(1, 0xAA);
    // RTT가 5ms, 30ms, 200ms인 명령을 완료해 누적 버킷을 검증한다.
    for (int ms : {5, 30, 200}) {
        auto const id = f.send(device);
        f.clock.advance(std::chrono::milliseconds{ms});
        f.commands.settle(device, id, true, 0, f.clock.now());
    }

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"0.005\"} 1")); // 5ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"0.02\"} 1")); // 여전히 5ms만
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"0.05\"} 2")); // +30ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"0.25\"} 3")); // +200ms
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_bucket{le=\"+Inf\"} 3"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_count 3"));
    EXPECT_TRUE(contains(text, "ddcs_command_rtt_seconds_sum 0.235")); // 5+30+200ms
}

TEST(MetricsServiceTest, ScrapeReportsSupersedeAndStale) {
    Fixture f;
    auto const device = f.activate(1, 0xAA);

    auto const first = f.send(device);
    f.send(device);                                           // 같은 Device의 같은 종류 명령을 교체한다.
    f.commands.settle(device, first, true, 0, f.clock.now()); // 교체된 명령의 응답은 무시한다.

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "ddcs_commands_superseded_total 1"));
    EXPECT_TRUE(contains(text, "ddcs_command_stale_responses_total 1"));
}

TEST(MetricsServiceTest, ScrapeReflectsLivenessAndHandshakeCloseReasons) {
    Fixture f;
    f.activate(1, 0xAA);
    EXPECT_TRUE(f.sessions.add(ConnectionId{2}, f.clock.now())); // 연결 후 등록을 완료하지 않은 상태

    f.clock.advance(4s); // 응답 없이 연결 유지 기한 3초를 넘긴다.
    f.session_service.sweep(f.clock.now());

    auto const text = f.metrics.scrape();
    EXPECT_TRUE(contains(text, "ddcs_connections_closed_total{reason=\"liveness_expired\"} 1"));
    EXPECT_TRUE(contains(text, "ddcs_connections_closed_total{reason=\"handshake_expired\"} 1"));
}

TEST(MetricsServiceTest, ScrapeExportsTheBoundedConnectionCloseReasonVocabulary) {
    Fixture f;
    using ddcs::ctrl::app::transport::port::disconnect_reasons;
    using ddcs::ctrl::app::transport::port::to_string;

    std::uint64_t connection = 1;
    for (auto const reason : disconnect_reasons) {
        f.session_service.on_connected(ConnectionId{connection});
        f.session_service.on_disconnected(ConnectionId{connection}, reason);
        ++connection;
    }

    auto const text = f.metrics.scrape();
    for (auto const reason : disconnect_reasons) {
        std::string const sample =
            "ddcs_connections_closed_total{reason=\"" + std::string{to_string(reason)} + "\"} 1";
        EXPECT_TRUE(contains(text, sample));
    }
    EXPECT_FALSE(contains(text, "register_undelivered"));
}

TEST(MetricsServiceTest, ScrapeReportsGroupGauges) {
    using ddcs::ctrl::domain::GroupRule;
    using ddcs::device::Mode;
    using ddcs::device::Status;
    Fixture f;
    // 집계 대상인 zone_a와 zone_b를 정책에 등록한다.
    f.policy.set("zone_a", *GroupRule::create(70, 30, Mode::performance, Mode::normal));
    f.policy.set("zone_b", *GroupRule::create(60, 45, Mode::performance, Mode::safe));

    // zone_a에는 performance 모드인 Device 2개가 있고, 부하 평균은 70이다.
    auto const a1 = f.activate(1, 0x01);
    auto const a2 = f.activate(2, 0x02);
    f.devices.enroll(a1, "zone_a");
    EXPECT_TRUE(
        f.devices.update_status(a1, Status{.mode = Mode::performance, .load = 80.0, .temp = 50.0})
    );
    f.devices.enroll(a2, "zone_a");
    EXPECT_TRUE(
        f.devices.update_status(a2, Status{.mode = Mode::performance, .load = 60.0, .temp = 60.0})
    );
    // zone_b에는 부하가 10인 safe 모드 Device 1개가 있다.
    auto const b1 = f.activate(3, 0x03);
    f.devices.enroll(b1, "zone_b");
    EXPECT_TRUE(f.devices.update_status(b1, Status{.mode = Mode::safe, .load = 10.0, .temp = 30.0})
    );

    auto const text = f.metrics.scrape();

    EXPECT_TRUE(contains(text, "# TYPE ddcs_group_load_ratio gauge"));
    EXPECT_TRUE(contains(text, "ddcs_group_load_ratio{group=\"zone_a\"} 0.7")); // (80+60)/2/100
    EXPECT_TRUE(contains(text, "ddcs_group_load_ratio{group=\"zone_b\"} 0.1"));
    EXPECT_TRUE(contains(text, "ddcs_group_temperature_celsius{group=\"zone_a\"} 55")); // (50+60)/2
    EXPECT_TRUE(contains(text, "ddcs_group_temperature_celsius{group=\"zone_b\"} 30"));
    EXPECT_TRUE(contains(text, "ddcs_group_devices{group=\"zone_a\",mode=\"performance\"} 2"));
    EXPECT_TRUE(contains(text, "ddcs_group_devices{group=\"zone_a\",mode=\"safe\"} 0"));
    EXPECT_TRUE(contains(text, "ddcs_group_devices{group=\"zone_b\",mode=\"safe\"} 1"));
}

} // namespace
