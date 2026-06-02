#include "ddcs/ctrl/app/agent/register_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
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
using ddcs::ctrl::app::agent::RegisterService;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::session::State;
using ddcs::ctrl::domain::agent::AgentRegistry;
using ddcs::ctrl::port::transport::CloseMode;
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

PoolHandle<LinearBuffer> make_register_body(Uuid uuid, std::string group = "", std::string version = "") {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{256});
    auto buf = pool.acquire();
    msg::RegisterRequest const req{.agent_uuid = uuid, .group = std::move(group), .version = std::move(version)};
    EXPECT_TRUE(msg::encode(req, *buf));
    return buf;
}

struct Fixture {
    SessionRegistry sessions;
    AgentRegistry registry;
    MockOutbound outbound;
    ManualClock clock;
    RegisterService svc{sessions, registry, outbound, clock};
};

} // namespace

TEST(RegisterServiceTest, BindsAgentActivatesAndSendsResponse) {
    Fixture f;
    f.sessions.open(ConnectionId{1});
    f.svc.handle_register(ConnectionId{1}, make_register_body(make_uuid(1)));

    auto* s = f.sessions.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state, State::active);
    EXPECT_TRUE(s->agent.valid());

    ASSERT_EQ(f.outbound.sends.size(), 1u);
    EXPECT_EQ(f.outbound.sends[0].id, ConnectionId{1});
    EXPECT_EQ(f.outbound.sends[0].type, static_cast<std::uint8_t>(msg::Type::RegisterResponse));

    msg::RegisterResponse resp{};
    auto const& b = f.outbound.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(b.data()), b.size()}, resp));
    EXPECT_EQ(resp.result, msg::RegisterResult::success);
    EXPECT_TRUE(f.outbound.closes.empty()); // 첫 등록 -> kick 없음
}

TEST(RegisterServiceTest, StoresDeclaredGroupAndVersion) {
    Fixture f;
    f.sessions.open(ConnectionId{1});
    f.svc.handle_register(ConnectionId{1}, make_register_body(make_uuid(1), "sensors", "1.2.3"));

    auto const* a = f.registry.find_by_uuid(make_uuid(1));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->group, "sensors");
    EXPECT_EQ(a->version, "1.2.3");
}

TEST(RegisterServiceTest, KicksOldConnectionAndCountsOnSameUuidReRegister) {
    Fixture f;
    f.sessions.open(ConnectionId{1});
    f.svc.handle_register(ConnectionId{1}, make_register_body(make_uuid(1)));
    f.sessions.open(ConnectionId{2});
    f.svc.handle_register(ConnectionId{2}, make_register_body(make_uuid(1))); // 같은 uuid -> conn1 kick

    EXPECT_EQ(f.svc.kicked_total(), 1u);
    ASSERT_FALSE(f.outbound.closes.empty());
    EXPECT_EQ(f.outbound.closes.back().first, ConnectionId{1}); // 옛 conn 축출
    EXPECT_EQ(f.outbound.closes.back().second, CloseMode::force);
}

TEST(RegisterServiceTest, SetsLastSeenToNow) {
    Fixture f;
    f.clock.advance(std::chrono::seconds{10});
    f.sessions.open(ConnectionId{1});
    f.svc.handle_register(ConnectionId{1}, make_register_body(make_uuid(1)));

    auto* s = f.sessions.find(ConnectionId{1});
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->last_seen, f.clock.now()); // 등록 = 첫 활동 -> liveness 시작점
}

TEST(RegisterServiceTest, ReRegisterSameAgentKicksOldConnection) {
    Fixture f;
    auto const uuid = make_uuid(7);
    f.sessions.open(ConnectionId{1});
    f.svc.handle_register(ConnectionId{1}, make_register_body(uuid));
    f.sessions.open(ConnectionId{2});
    f.svc.handle_register(ConnectionId{2}, make_register_body(uuid)); // 같은 agent, 새 conn

    ASSERT_EQ(f.outbound.closes.size(), 1u);
    EXPECT_EQ(f.outbound.closes[0].first, ConnectionId{1}); // 옛 conn kick
    EXPECT_EQ(f.outbound.closes[0].second, CloseMode::force);

    auto* s2 = f.sessions.find(ConnectionId{2});
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(f.sessions.resolve(s2->agent), ConnectionId{2}); // 현재 바인딩 = 새 conn
}

TEST(RegisterServiceTest, DecodeFailClosesConnection) {
    Fixture f;
    f.sessions.open(ConnectionId{1});
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 4, std::size_t{64});
    auto bad = pool.acquire();
    std::array<std::byte, 4> junk{};
    ASSERT_TRUE(bad->write({junk.data(), junk.size()}));
    f.svc.handle_register(ConnectionId{1}, std::move(bad));

    ASSERT_EQ(f.outbound.closes.size(), 1u);
    EXPECT_EQ(f.outbound.closes[0].first, ConnectionId{1});
    EXPECT_TRUE(f.outbound.sends.empty()); // 응답 없음
}
