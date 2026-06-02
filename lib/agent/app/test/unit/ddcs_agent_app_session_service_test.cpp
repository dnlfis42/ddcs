#include "ddcs/agent/app/session_service.hpp"

#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/agent/port/outbound.hpp"
#include "ddcs/agent/port/timer_id.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/proto/cmd/command.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace {

using ddcs::agent::app::SessionService;
using ddcs::agent::domain::DummyDevice;
using ddcs::agent::port::Outbound;
using ddcs::agent::port::TimerId;
using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
namespace msg = ddcs::proto::msg;

class MockOutbound : public Outbound {
public:
    ddcs::common::ObjectPool<LinearBuffer> pool{ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{1024})};

    struct Sent {
        std::uint8_t type;
        std::string body;
    };
    std::vector<Sent> sends;
    std::vector<std::pair<TimerId, std::chrono::nanoseconds>> timers;
    std::vector<TimerId> cancels;
    int closes{0};

    PoolHandle<LinearBuffer> payload_buffer() override { return pool.acquire(); }
    void send(std::uint8_t type, PoolHandle<LinearBuffer> body) override {
        auto const r = body->readable();
        sends.push_back({type, std::string{reinterpret_cast<char const*>(r.data()), r.size()}});
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

template <typename T>
PoolHandle<LinearBuffer> body_of(T const& m) {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{256});
    auto buf = pool.acquire();
    EXPECT_TRUE(msg::encode(m, *buf));
    return buf;
}

constexpr std::uint8_t kType(msg::Type t) { return static_cast<std::uint8_t>(t); }

namespace cmd = ddcs::proto::cmd;

std::string encode_setmode(ddcs::device::Mode mode) {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{64});
    auto buf = pool.acquire();
    EXPECT_TRUE(cmd::encode(cmd::SetMode{.mode = mode}, *buf));
    auto const r = buf->readable();
    return std::string{reinterpret_cast<char const*>(r.data()), r.size()};
}

// frame.type=Command 의 body: Command{command_id, type=SetMode, payload=encode(SetMode)}.
PoolHandle<LinearBuffer> setmode_command(std::uint64_t id, ddcs::device::Mode mode) {
    return body_of(
        msg::Command{
            .command_id = id,
            .type = static_cast<std::uint8_t>(cmd::CommandType::SetMode),
            .payload = encode_setmode(mode),
        }
    );
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

// connect->register 성공으로 active 진입 후 outbound 기록을 비운다.
void activate(SessionService& svc, MockOutbound& out) {
    svc.on_connected();
    svc.on_recv(
        kType(msg::Type::RegisterResponse),
        body_of(msg::RegisterResponse{.result = msg::RegisterResult::success, .reason = {}})
    );
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
    EXPECT_EQ(out.sends[0].type, kType(msg::Type::RegisterRequest));
    msg::RegisterRequest req{};
    auto const& b = out.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(b.data()), b.size()}, req));
    EXPECT_EQ(req.agent_uuid, make_uuid(0xab));
    EXPECT_TRUE(has_timer(out, TimerId::register_timeout));
}

TEST(AgentSessionServiceTest, RegisterRequestCarriesConfiguredGroupAndVersion) {
    DummyDevice device;
    MockOutbound out;
    SessionService::Config cfg{};
    cfg.group = "sensors";
    cfg.version = "1.2.3";
    SessionService svc{make_uuid(0xcd), device, out, cfg};

    svc.on_connected();

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(out.sends[0].type, kType(msg::Type::RegisterRequest));
    msg::RegisterRequest req{};
    auto const& b = out.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(b.data()), b.size()}, req));
    EXPECT_EQ(req.group, "sensors");
    EXPECT_EQ(req.version, "1.2.3");
}

TEST(AgentSessionServiceTest, RegisterResponseSuccessEntersActive) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_recv(
        kType(msg::Type::RegisterResponse),
        body_of(msg::RegisterResponse{.result = msg::RegisterResult::success, .reason = {}})
    );

    EXPECT_EQ(svc.state(), SessionService::State::active);
    EXPECT_TRUE(has_cancel(out, TimerId::register_timeout));
    EXPECT_EQ(out.closes, 0);
}

TEST(AgentSessionServiceTest, RegisterResponseFailedCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected();

    svc.on_recv(
        kType(msg::Type::RegisterResponse),
        body_of(msg::RegisterResponse{.result = msg::RegisterResult::failed, .reason = "busy"})
    );

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

    svc.on_recv(kType(msg::Type::Heartbeat), body_of(msg::Heartbeat{.timestamp_ms = 0}));

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

    svc.on_recv(
        kType(msg::Type::RegisterResponse),
        body_of(msg::RegisterResponse{.result = msg::RegisterResult::success, .reason = {}})
    );

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
    EXPECT_EQ(out.sends[0].type, kType(msg::Type::Heartbeat));
    EXPECT_EQ(count_timer(out, TimerId::heartbeat), 1); // 재무장
}

TEST(AgentSessionServiceTest, StatusTimerSendsStatusFromDeviceAndReschedules) {
    DummyDevice device{ddcs::device::Mode::performance};
    device.set_load(80);
    device.set_temp(55);
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_timer(TimerId::status);

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(out.sends[0].type, kType(msg::Type::Status));
    msg::Status st{};
    auto const& b = out.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(b.data()), b.size()}, st));
    EXPECT_NE(st.status_json.find("performance"), std::string::npos);  // mode 반영
    EXPECT_NE(st.status_json.find(R"("load":80)"), std::string::npos); // load 반영
    EXPECT_NE(st.status_json.find(R"("temp":55)"), std::string::npos); // temp 반영
    EXPECT_EQ(count_timer(out, TimerId::status), 1);                   // 재무장
}

TEST(AgentSessionServiceTest, HeartbeatTimerIgnoredWhenNotActive) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    svc.on_connected(); // registering, not active

    svc.on_timer(TimerId::heartbeat);

    EXPECT_TRUE(out.sends.empty() || out.sends[0].type != kType(msg::Type::Heartbeat));
}

TEST(AgentSessionServiceTest, CommandAppliesToDeviceAndAcksThenOutcomes) {
    DummyDevice device{ddcs::device::Mode::safe};
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(kType(msg::Type::Command), setmode_command(1, ddcs::device::Mode::performance));

    EXPECT_EQ(device.mode(), ddcs::device::Mode::performance); // device 적용
    ASSERT_EQ(out.sends.size(), 2u);
    EXPECT_EQ(out.sends[0].type, kType(msg::Type::CommandAck)); // ACK 먼저
    EXPECT_EQ(out.sends[1].type, kType(msg::Type::CommandOutcome));

    msg::CommandAck ack{};
    auto const& a = out.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(a.data()), a.size()}, ack));
    EXPECT_EQ(ack.command_id, 1u);

    msg::CommandOutcome outcome{};
    auto const& o = out.sends[1].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(o.data()), o.size()}, outcome));
    EXPECT_EQ(outcome.command_id, 1u);
    EXPECT_EQ(outcome.result, msg::CommandResult::success);
}

TEST(AgentSessionServiceTest, DuplicateCommandResendsWithoutReapplying) {
    DummyDevice device{ddcs::device::Mode::safe};
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(kType(msg::Type::Command), setmode_command(1, ddcs::device::Mode::performance));
    ASSERT_EQ(device.mode(), ddcs::device::Mode::performance);
    out.sends.clear();

    // 같은 command_id=1 로 다른 mode -> dedup: 재적용 안 함(device 유지) + 응답만 재송신.
    svc.on_recv(kType(msg::Type::Command), setmode_command(1, ddcs::device::Mode::safe));

    EXPECT_EQ(device.mode(), ddcs::device::Mode::performance); // 재적용 안 됨
    ASSERT_EQ(out.sends.size(), 2u);                           // ACK+Outcome 재송신
    EXPECT_EQ(out.sends[0].type, kType(msg::Type::CommandAck));
    EXPECT_EQ(out.sends[1].type, kType(msg::Type::CommandOutcome));
}

TEST(AgentSessionServiceTest, UnknownCommandTypeOutcomesFailed) {
    DummyDevice device{ddcs::device::Mode::safe};
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(
        kType(msg::Type::Command), body_of(msg::Command{.command_id = 5, .type = 0xFF, .payload = {}})
    ); // 미지 CommandType

    EXPECT_EQ(device.mode(), ddcs::device::Mode::safe); // device 변동 없음
    ASSERT_EQ(out.sends.size(), 2u);
    msg::CommandOutcome outcome{};
    auto const& o = out.sends[1].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(o.data()), o.size()}, outcome));
    EXPECT_EQ(outcome.result, msg::CommandResult::failed);
}

TEST(AgentSessionServiceTest, UnexpectedTypeWhileActiveCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{make_uuid(1), device, out};
    activate(svc, out);

    svc.on_recv(kType(msg::Type::Heartbeat), body_of(msg::Heartbeat{.timestamp_ms = 0}));

    EXPECT_EQ(out.closes, 1);
}
