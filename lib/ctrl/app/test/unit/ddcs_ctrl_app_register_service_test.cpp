#include "ddcs/ctrl/app/agent/register_service.hpp"

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::ctrl::app::agent::RegisterService;
using ddcs::ctrl::domain::DeviceRegistry;
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

// identity/ack 전용 RegisterService. 세션 바인딩/kick 은 SessionManager 소관(session_manager_test 가 검증).
struct Fixture {
    DeviceRegistry registry;
    MockOutbound outbound;
    RegisterService svc{registry, outbound};
};

} // namespace

TEST(RegisterServiceTest, ResolveCreatesAgentAndReturnsValidId) {
    Fixture f;
    auto const id = f.svc.resolve(ConnectionId{1}, make_register_body(make_uuid(1)));
    EXPECT_TRUE(id.valid());
    EXPECT_NE(f.registry.find(make_uuid(1)), nullptr);
}

TEST(RegisterServiceTest, ResolveReturnsSameIdForSameUuid) {
    Fixture f;
    auto const a = f.svc.resolve(ConnectionId{1}, make_register_body(make_uuid(1)));
    auto const b = f.svc.resolve(ConnectionId{2}, make_register_body(make_uuid(1)));
    EXPECT_TRUE(a.valid());
    EXPECT_EQ(a, b); // 영속 identity (재접속 가로질러 안정)
}

TEST(RegisterServiceTest, ResolveStoresDeclaredGroupAndVersion) {
    Fixture f;
    f.svc.resolve(ConnectionId{1}, make_register_body(make_uuid(1), "sensors", "1.2.3"));
    auto const* a = f.registry.find(make_uuid(1));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->group, "sensors");
    EXPECT_EQ(a->version, "1.2.3");
}

TEST(RegisterServiceTest, ResolveReturnsInvalidOnDecodeFail) {
    Fixture f;
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 4, std::size_t{64});
    auto bad = pool.acquire();
    std::array<std::byte, 4> junk{};
    ASSERT_TRUE(bad->write({junk.data(), junk.size()}));
    auto const id = f.svc.resolve(ConnectionId{1}, std::move(bad));
    EXPECT_FALSE(id.valid());          // 식별 불가
    EXPECT_TRUE(f.outbound.sends.empty()); // 응답 없음 (close 는 호출자 SessionManager 소관)
}

TEST(RegisterServiceTest, SendRegisterResponseSendsSuccessFrame) {
    Fixture f;
    f.svc.send_register_response(ConnectionId{1}, true);

    ASSERT_EQ(f.outbound.sends.size(), 1u);
    EXPECT_EQ(f.outbound.sends[0].id, ConnectionId{1});
    EXPECT_EQ(f.outbound.sends[0].type, static_cast<std::uint8_t>(msg::Type::RegisterResponse));

    msg::RegisterResponse resp{};
    auto const& b = f.outbound.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(b.data()), b.size()}, resp));
    EXPECT_EQ(resp.result, msg::RegisterResult::success);
}
