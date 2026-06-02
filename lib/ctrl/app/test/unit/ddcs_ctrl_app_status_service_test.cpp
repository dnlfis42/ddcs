#include "ddcs/ctrl/app/agent/status_service.hpp"

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/proto/msg/message.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::ctrl::app::agent::StatusService;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::agent::AgentId;
using ddcs::ctrl::domain::agent::AgentRegistry;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::device::Mode;
namespace msg = ddcs::proto::msg;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return Uuid{b};
}

PoolHandle<LinearBuffer> make_status_body(std::string json) {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{256});
    auto buf = pool.acquire();
    msg::Status const st{.timestamp_ms = 0, .status_json = std::move(json)};
    EXPECT_TRUE(msg::encode(st, *buf));
    return buf;
}

struct Fixture {
    SessionRegistry sessions;
    AgentRegistry registry;
    StatusService svc{sessions, registry};

    AgentId activate(ConnectionId conn, Uuid uuid) { // 등록 + 세션 active 바인딩
        auto const id = registry.find_or_create(uuid).id;
        sessions.open(conn);
        sessions.bind(conn, id, {});
        return id;
    }
};

} // namespace

TEST(StatusServiceTest, UpdatesAgentTelemetryFromStatus) {
    Fixture f;
    auto const id = f.activate(ConnectionId{1}, make_uuid(1));

    f.svc.handle_status(ConnectionId{1}, make_status_body(R"({"mode":"performance","load":75,"temp":50.5})"));

    auto const* a = f.registry.find_by_id(id);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->mode, Mode::performance);
    EXPECT_EQ(a->load, 75.0);
    EXPECT_EQ(a->temp, 50.5);
}

TEST(StatusServiceTest, DropsStatusFromInactiveConnection) {
    Fixture f;
    auto const id = f.registry.find_or_create(make_uuid(2)).id;
    f.sessions.open(ConnectionId{2}); // handshaking - active 아님

    f.svc.handle_status(ConnectionId{2}, make_status_body(R"({"mode":"normal","load":10,"temp":20})"));

    auto const* a = f.registry.find_by_id(id);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->mode, Mode::safe); // 갱신 안 됨(기본값 유지)
    EXPECT_EQ(a->load, 0.0);
}

TEST(StatusServiceTest, DropsUndecodableStatusSilently) {
    Fixture f;
    f.activate(ConnectionId{3}, make_uuid(3));
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 4, std::size_t{64});
    auto bad = pool.acquire();
    std::array<std::byte, 3> junk{}; // timestamp(8B) 미달 -> decode 실패
    ASSERT_TRUE(bad->write({junk.data(), junk.size()}));
    f.svc.handle_status(ConnectionId{3}, std::move(bad));
    SUCCEED(); // 조용히 드롭(크래시 없음)
}

TEST(StatusServiceTest, DropsBadJsonSilently) {
    Fixture f;
    auto const id = f.activate(ConnectionId{4}, make_uuid(4));
    f.svc.handle_status(ConnectionId{4}, make_status_body("not json"));
    EXPECT_EQ(f.registry.find_by_id(id)->mode, Mode::safe); // 파싱 실패 -> 갱신 안 됨
}
