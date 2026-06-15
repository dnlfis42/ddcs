#include "ddcs/agent/app/session_service.hpp"

#include "ddcs/agent/app/port/outbound.hpp"
#include "ddcs/agent/app/port/timer_id.hpp"
#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/device/command.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/wire/acmp/message.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace acmp = ddcs::wire::acmp;

using ddcs::agent::app::SessionService;
using ddcs::agent::app::port::Outbound;
using ddcs::agent::app::port::TimerId;
using ddcs::agent::domain::DummyDevice;
using ddcs::common::LinearBuffer;
using ddcs::common::ObjectPool;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::device::Mode;

constexpr std::uint8_t outcome_success{0};
constexpr std::uint8_t outcome_failed{1};

// 송신 기록 대역. payload(acmp `[type][body]`)를 통째로 보관한다(infra가 frame header를 덧씌우기 전 상태).
class MockOutbound : public Outbound {
public:
    ObjectPool<LinearBuffer> pool{ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{1024})};

    std::vector<std::string> sends; // 각 원소가 acmp payload(`[type][body]`)
    std::vector<std::pair<TimerId, std::chrono::nanoseconds>> timers;
    std::vector<TimerId> cancels;
    int closes{0};

    PoolHandle<LinearBuffer> payload_buffer() override { return pool.acquire(); }
    void send(PoolHandle<LinearBuffer> message) override {
        auto const r = message->readable();
        sends.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }
    void schedule_timer(TimerId id, std::chrono::nanoseconds d) override { timers.emplace_back(id, d); }
    void cancel_timer(TimerId id) override { cancels.push_back(id); }
    void close() override { ++closes; }
};

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b.fill(std::byte{seed});
    return Uuid{b};
}

ObjectPool<LinearBuffer>& build_pool() {
    static auto pool = ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{256});
    return pool;
}

// 수신 payload(acmp `[type][body]`) 빌더.
PoolHandle<LinearBuffer> payload_register_outcome(std::uint8_t code) {
    auto buf = build_pool().acquire();
    auto const w = acmp::encode_register_outcome(code, buf->writable());
    EXPECT_TRUE(w.has_value());
    if (w) {
        buf->commit(*w);
    }
    return buf;
}
PoolHandle<LinearBuffer> payload_heartbeat() {
    auto buf = build_pool().acquire();
    auto const w = acmp::encode_heartbeat(buf->writable());
    EXPECT_TRUE(w.has_value());
    if (w) {
        buf->commit(*w);
    }
    return buf;
}
// command_request: [type][command_id][command_type] 헤더 뒤에 device payload를 append.
PoolHandle<LinearBuffer> payload_command(std::uint64_t id, std::uint8_t command_type, std::span<std::byte const> cmd) {
    auto buf = build_pool().acquire();
    auto const w = acmp::encode_command_request_header(id, command_type, buf->writable());
    EXPECT_TRUE(w.has_value());
    if (w) {
        buf->commit(*w);
    }
    if (!cmd.empty()) {
        EXPECT_TRUE(buf->write(cmd));
    }
    return buf;
}
PoolHandle<LinearBuffer> setmode_command(std::uint64_t id, Mode mode) {
    static auto cmd_pool = ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{64});
    auto cmd_buf = cmd_pool.acquire();
    EXPECT_TRUE(ddcs::device::encode(ddcs::device::SetMode{.mode = mode}, *cmd_buf));
    return payload_command(id, static_cast<std::uint8_t>(ddcs::device::CommandType::set_mode), cmd_buf->readable());
}

acmp::MessageType sent_type(std::string const& s) {
    return acmp::peek_type({reinterpret_cast<std::byte const*>(s.data()), s.size()});
}
std::span<std::byte const> sent_body(std::string const& s) {
    std::span<std::byte const> const bytes{reinterpret_cast<std::byte const*>(s.data()), s.size()};
    return bytes.subspan(1);
}

bool has_timer(MockOutbound const& o, TimerId id) {
    for (auto const& [t, d] : o.timers) {
        if (t == id) {
            return true;
        }
    }
    return false;
}

bool has_cancel(MockOutbound const& o, TimerId id) {
    for (auto const c : o.cancels) {
        if (c == id) {
            return true;
        }
    }
    return false;
}

int count_timer(MockOutbound const& o, TimerId id) {
    int n = 0;
    for (auto const& [t, d] : o.timers) {
        if (t == id) {
            ++n;
        }
    }
    return n;
}

// connect 후 register 3-way 성공으로 active 진입 후 outbound 기록을 비운다.
void activate(SessionService& svc, MockOutbound& out) {
    svc.on_connected();
    svc.on_recv(payload_register_outcome(outcome_success));
    out.sends.clear();
    out.timers.clear();
    out.cancels.clear();
}

} // namespace

TEST(AgentSessionServiceTest, OnConnectedSendsRegisterAndArmsTimeout) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(0xab), device, out};

    svc.on_connected();

    EXPECT_EQ(svc.state(), SessionService::State::registering);
    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), acmp::MessageType::register_request);
    auto const req = acmp::decode_register_request(sent_body(out.sends[0]));
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->id, make_uuid(0xab));
    EXPECT_TRUE(has_timer(out, TimerId::register_timeout));
}

TEST(AgentSessionServiceTest, RegisterRequestCarriesConfiguredGroup) {
    DummyDevice device;
    MockOutbound out;
    SessionService::Config cfg{};
    cfg.group = "sensors";
    SessionService svc{make_uuid(0xcd), device, out, cfg};

    svc.on_connected();

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), acmp::MessageType::register_request);
    auto const req = acmp::decode_register_request(sent_body(out.sends[0]));
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->group, "sensors");
}

TEST(AgentSessionServiceTest, RegisterOutcomeSuccessSendsAckAndEntersActive) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_recv(payload_register_outcome(outcome_success));

    EXPECT_EQ(svc.state(), SessionService::State::active);
    EXPECT_TRUE(has_cancel(out, TimerId::register_timeout));
    EXPECT_EQ(out.closes, 0);
    // 3-way: register_request 뒤에 register_ack 송신
    ASSERT_EQ(out.sends.size(), 2u);
    EXPECT_EQ(sent_type(out.sends[1]), acmp::MessageType::register_ack);
}

TEST(AgentSessionServiceTest, RegisterOutcomeFailedCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_recv(payload_register_outcome(outcome_failed));

    EXPECT_NE(svc.state(), SessionService::State::active);
    EXPECT_EQ(out.closes, 1);
}

TEST(AgentSessionServiceTest, RegisterTimeoutClosesConnection) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_timer(TimerId::register_timeout);

    EXPECT_EQ(svc.state(), SessionService::State::closing);
    EXPECT_EQ(out.closes, 1);
}

TEST(AgentSessionServiceTest, UnexpectedTypeWhileRegisteringCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_recv(payload_heartbeat());

    EXPECT_EQ(out.closes, 1);
}

TEST(AgentSessionServiceTest, DisconnectResetsToIdle) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_disconnected();

    EXPECT_EQ(svc.state(), SessionService::State::idle);
    EXPECT_TRUE(has_cancel(out, TimerId::register_timeout));
}

TEST(AgentSessionServiceTest, EnterActiveArmsHeartbeatAndStatusTimers) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_recv(payload_register_outcome(outcome_success));

    EXPECT_TRUE(has_timer(out, TimerId::heartbeat));
    EXPECT_TRUE(has_timer(out, TimerId::status));
}

TEST(AgentSessionServiceTest, HeartbeatTimerSendsHeartbeatAndReschedules) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_timer(TimerId::heartbeat);

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), acmp::MessageType::heartbeat);
    EXPECT_EQ(count_timer(out, TimerId::heartbeat), 1); // 재무장
}

TEST(AgentSessionServiceTest, StatusTimerSendsStatusFromDeviceAndReschedules) {
    DummyDevice device{Mode::performance};
    device.set_load(80);
    device.set_temp(55);
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_timer(TimerId::status);

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), acmp::MessageType::status);
    auto const st = acmp::decode_status(sent_body(out.sends[0]));
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(st->mode, static_cast<std::uint8_t>(Mode::performance)); // mode 반영
    EXPECT_DOUBLE_EQ(st->load, 80.0);                                  // load 반영
    EXPECT_DOUBLE_EQ(st->temp, 55.0);                                  // temp 반영
    EXPECT_EQ(count_timer(out, TimerId::status), 1);                   // 재무장
}

TEST(AgentSessionServiceTest, HeartbeatTimerIgnoredWhenNotActive) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected(); // registering, not active

    svc.on_timer(TimerId::heartbeat);

    for (auto const& s : out.sends) {
        EXPECT_NE(sent_type(s), acmp::MessageType::heartbeat);
    }
}

TEST(AgentSessionServiceTest, CommandAppliesToDeviceAndAcksThenOutcomes) {
    DummyDevice device{Mode::safe};
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(setmode_command(1, Mode::performance));

    EXPECT_EQ(device.mode(), Mode::performance); // device 적용
    ASSERT_EQ(out.sends.size(), 2u);
    EXPECT_EQ(sent_type(out.sends[0]), acmp::MessageType::command_ack); // ACK 먼저
    EXPECT_EQ(sent_type(out.sends[1]), acmp::MessageType::command_outcome);

    auto const ack = acmp::decode_command_ack(sent_body(out.sends[0]));
    ASSERT_TRUE(ack.has_value());
    EXPECT_EQ(ack->command_id, 1u);

    auto const outcome = acmp::decode_command_outcome(sent_body(out.sends[1]));
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(outcome->command_id, 1u);
    EXPECT_EQ(outcome->code, outcome_success);
}

TEST(AgentSessionServiceTest, DuplicateCommandResendsWithoutReapplying) {
    DummyDevice device{Mode::safe};
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(setmode_command(1, Mode::performance));
    ASSERT_EQ(device.mode(), Mode::performance);
    out.sends.clear();

    // 같은 command_id=1로 다른 mode면 dedup: 재적용 안 함(device 유지) + 응답만 재송신.
    svc.on_recv(setmode_command(1, Mode::safe));

    EXPECT_EQ(device.mode(), Mode::performance); // 재적용 안 됨
    ASSERT_EQ(out.sends.size(), 2u);             // ACK+Outcome 재송신
    EXPECT_EQ(sent_type(out.sends[0]), acmp::MessageType::command_ack);
    EXPECT_EQ(sent_type(out.sends[1]), acmp::MessageType::command_outcome);
}

TEST(AgentSessionServiceTest, UnknownCommandTypeOutcomesFailed) {
    DummyDevice device{Mode::safe};
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(payload_command(5, 0xFF, {})); // 미지 CommandType

    EXPECT_EQ(device.mode(), Mode::safe); // device 변동 없음
    ASSERT_EQ(out.sends.size(), 2u);
    auto const outcome = acmp::decode_command_outcome(sent_body(out.sends[1]));
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(outcome->code, outcome_failed);
}

TEST(AgentSessionServiceTest, UnexpectedTypeWhileActiveCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(payload_heartbeat());

    EXPECT_EQ(out.closes, 1);
}
