#include "ddcs/ctrl/app/session/session_manager.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/agent/register_service.hpp"
#include "ddcs/ctrl/app/agent/status_service.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/inbound.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"
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

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::app::agent::RegisterService;
using ddcs::ctrl::app::agent::StatusService;
using ddcs::ctrl::app::session::SessionManager;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::session::State;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::port::transport::CloseMode;
using ddcs::ctrl::port::transport::CloseReason;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;
namespace msg = ddcs::proto::msg;

class MockOutbound : public Outbound {
public:
    ddcs::common::ObjectPool<LinearBuffer> pool{ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{1024})};

    struct Sent {
        ConnectionId id;
        std::uint8_t type;
        std::string body;
    };
    std::vector<Sent> sends;
    std::vector<std::pair<ConnectionId, CloseMode>> closes;

    PoolHandle<LinearBuffer> payload_buffer() override { return pool.acquire(); }
    void send(ConnectionId id, std::uint8_t type, PoolHandle<LinearBuffer> body) override {
        auto const r = body->readable();
        sends.push_back({id, type, std::string{reinterpret_cast<char const*>(r.data()), r.size()}});
    }
    void close(ConnectionId id, CloseMode mode) override { closes.emplace_back(id, mode); }
};

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return Uuid{b};
}

template <typename T>
PoolHandle<LinearBuffer> encode_body(T const& m) {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{256});
    auto buf = pool.acquire();
    EXPECT_TRUE(msg::encode(m, *buf));
    return buf;
}

constexpr std::uint8_t kType(msg::Type t) { return static_cast<std::uint8_t>(t); }

// SessionManager 와 그 뒤의 실 use-case 그래프를 묶은 테스트 하니스.
struct Harness {
    SessionRegistry sessions;
    DeviceRegistry registry;
    MockOutbound outbound;
    ManualClock clock;
    RegisterService registrar{registry, outbound};
    StatusService status{sessions, registry};
    CommandService commands{sessions, outbound, clock, std::chrono::seconds{5}};
    SessionManager mgr{sessions, registrar, status, commands, outbound, clock};

    void register_active(ConnectionId conn, Uuid uuid) {
        mgr.on_connect(conn);
        mgr.on_recv(conn, kType(msg::Type::RegisterRequest), encode_body(msg::RegisterRequest{.agent_uuid = uuid}));
        outbound.sends.clear();
        outbound.closes.clear();
    }
};

} // namespace

TEST(SessionManagerTest, OnConnectOpensHandshakingSession) {
    Harness h;
    h.mgr.on_connect(ConnectionId{1});
    auto* s = h.sessions.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, State::handshaking);
}

TEST(SessionManagerTest, RoutesRegisterRequestActivatesSession) {
    Harness h;
    h.mgr.on_connect(ConnectionId{1});
    h.mgr.on_recv(
        ConnectionId{1}, kType(msg::Type::RegisterRequest),
        encode_body(msg::RegisterRequest{.agent_uuid = make_uuid(1)})
    );

    auto* s = h.sessions.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, State::active);
    ASSERT_EQ(h.outbound.sends.size(), 1u); // RegisterResponse
    EXPECT_EQ(h.outbound.sends[0].type, kType(msg::Type::RegisterResponse));
}

TEST(SessionManagerTest, ActiveRecvUpdatesLastSeen) {
    Harness h;
    h.register_active(ConnectionId{1}, make_uuid(1)); // last_seen = t0
    h.clock.advance(std::chrono::seconds{2});
    h.mgr.on_recv(ConnectionId{1}, kType(msg::Type::Heartbeat), encode_body(msg::Heartbeat{.timestamp_ms = 0}));

    auto* s = h.sessions.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->last_seen, h.clock.now()); // active 면 *어떤* 메시지든 liveness 갱신
    EXPECT_TRUE(h.outbound.closes.empty());
}

TEST(SessionManagerTest, RoutesStatusAsTelemetry) {
    Harness h;
    h.register_active(ConnectionId{1}, make_uuid(1));
    h.mgr.on_recv(
        ConnectionId{1}, kType(msg::Type::Status), encode_body(msg::Status{.timestamp_ms = 0, .status_json = "{}"})
    );
    EXPECT_TRUE(h.outbound.closes.empty()); // 비치명적
}

TEST(SessionManagerTest, RoutesCommandOutcomeResolvesPending) {
    Harness h;
    h.sessions.open(ConnectionId{1});
    h.sessions.bind(ConnectionId{1}, make_uuid(1), h.clock.now());
    auto const command_id = h.commands.dispatch(make_uuid(1), 0x01, "payload");
    ASSERT_EQ(h.commands.pending_count(), 1u);

    h.mgr.on_recv(
        ConnectionId{1}, kType(msg::Type::CommandOutcome),
        encode_body(msg::CommandOutcome{.command_id = command_id, .result = msg::CommandResult::success, .reason = {}})
    );
    EXPECT_EQ(h.commands.pending_count(), 0u); // outcome 라우팅 -> 해소
}

TEST(SessionManagerTest, RoutesCommandAckExtendsDeadline) {
    Harness h;
    h.sessions.open(ConnectionId{1});
    h.sessions.bind(ConnectionId{1}, make_uuid(1), h.clock.now());
    auto const command_id = h.commands.dispatch(make_uuid(1), 0x01, "payload"); // deadline t0+5s

    h.clock.advance(std::chrono::seconds{4});
    h.mgr.on_recv(
        ConnectionId{1}, kType(msg::Type::CommandAck), encode_body(msg::CommandAck{.command_id = command_id})
    ); // deadline -> t0+9s

    h.clock.advance(std::chrono::seconds{4}); // t0+8s
    h.commands.sweep();
    EXPECT_EQ(h.commands.pending_count(), 1u); // ack 라우팅 -> deadline 연장 확인
}

TEST(SessionManagerTest, UnexpectedTypeClosesConnection) {
    Harness h;
    h.mgr.on_connect(ConnectionId{1});
    // RegisterResponse 는 c->a 전용 -> 수신 시 프로토콜 위반.
    h.mgr.on_recv(
        ConnectionId{1}, kType(msg::Type::RegisterResponse),
        encode_body(msg::RegisterResponse{.result = msg::RegisterResult::success, .reason = {}})
    );
    ASSERT_EQ(h.outbound.closes.size(), 1u);
    EXPECT_EQ(h.outbound.closes[0].first, ConnectionId{1});
    EXPECT_EQ(h.outbound.closes[0].second, CloseMode::force);
}

TEST(SessionManagerTest, CloseRequestMarksClosingAndGracefulOnPeerClosed) {
    Harness h;
    h.register_active(ConnectionId{1}, make_uuid(1));
    h.mgr.on_close_request(ConnectionId{1}, CloseReason::peer_closed);

    auto* s = h.sessions.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, State::closing); // liveness 에서 즉시 제외
    ASSERT_EQ(h.outbound.closes.size(), 1u);
    EXPECT_EQ(h.outbound.closes[0].second, CloseMode::graceful);
}

TEST(SessionManagerTest, CloseRequestErrorIsForce) {
    Harness h;
    h.mgr.on_close_request(ConnectionId{1}, CloseReason::conn_error);
    ASSERT_EQ(h.outbound.closes.size(), 1u);
    EXPECT_EQ(h.outbound.closes[0].second, CloseMode::force);
}

TEST(SessionManagerTest, DisconnectErasesSession) {
    Harness h;
    h.register_active(ConnectionId{1}, make_uuid(1));
    h.mgr.on_disconnect(ConnectionId{1});
    EXPECT_EQ(h.sessions.find(ConnectionId{1}), nullptr); // 세션 제거
}

TEST(SessionManagerTest, PreRegisterUnexpectedTypeCloses) {
    Harness h;
    h.mgr.on_connect(ConnectionId{1}); // handshaking
    // 등록 전 비-Register 프레임 -> 프로토콜 위반 -> close (register-or-die gap-fix).
    h.mgr.on_recv(
        ConnectionId{1}, kType(msg::Type::Status), encode_body(msg::Status{.timestamp_ms = 0, .status_json = "{}"})
    );
    ASSERT_EQ(h.outbound.closes.size(), 1u);
    EXPECT_EQ(h.outbound.closes[0].second, CloseMode::force);
}

TEST(SessionManagerTest, KicksOldConnectionOnSameUuidReRegister) {
    Harness h;
    h.register_active(ConnectionId{1}, make_uuid(1)); // conn1 active (sends/closes cleared)
    // 같은 uuid 가 새 conn 으로 재등록 -> 옛 conn 축출.
    h.mgr.on_connect(ConnectionId{2});
    h.mgr.on_recv(
        ConnectionId{2}, kType(msg::Type::RegisterRequest),
        encode_body(msg::RegisterRequest{.agent_uuid = make_uuid(1)})
    );

    EXPECT_EQ(h.mgr.kicked_total(), 1u);
    ASSERT_EQ(h.outbound.closes.size(), 1u);
    EXPECT_EQ(h.outbound.closes[0].first, ConnectionId{1}); // 옛 conn force close
    EXPECT_EQ(h.outbound.closes[0].second, CloseMode::force);
    EXPECT_EQ(h.sessions.resolve(make_uuid(1)), ConnectionId{2}); // 현재 바인딩 = 새 conn
}

TEST(SessionManagerTest, RegisterDecodeFailClosesConnection) {
    Harness h;
    h.mgr.on_connect(ConnectionId{1}); // handshaking
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 4, std::size_t{64});
    auto bad = pool.acquire();
    std::array<std::byte, 4> junk{};
    ASSERT_TRUE(bad->write({junk.data(), junk.size()}));
    h.mgr.on_recv(ConnectionId{1}, kType(msg::Type::RegisterRequest), std::move(bad));

    ASSERT_EQ(h.outbound.closes.size(), 1u);
    EXPECT_EQ(h.outbound.closes[0].first, ConnectionId{1});
    EXPECT_EQ(h.outbound.closes[0].second, CloseMode::force);
    EXPECT_TRUE(h.outbound.sends.empty()); // 응답 없음
    auto* s = h.sessions.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, State::handshaking); // 바인딩 안 됨
}
