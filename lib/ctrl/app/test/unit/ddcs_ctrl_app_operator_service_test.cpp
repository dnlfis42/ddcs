#include "ddcs/ctrl/app/ops/operator_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/proto/cmd/command.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::app::ops::OperatorService;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;
namespace cmd = ddcs::proto::cmd;
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

    PoolHandle<LinearBuffer> send_buffer() override { return pool.acquire(); }
    void send(ConnectionId id, std::uint8_t type, PoolHandle<LinearBuffer> body) override {
        auto const r = body->readable();
        sends.push_back({id, type, std::string{reinterpret_cast<char const*>(r.data()), r.size()}});
    }
    void drop(ConnectionId) override {}
};

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b.fill(std::byte{seed});
    return Uuid{b};
}

} // namespace

TEST(OperatorServiceTest, SetModeDispatchesCommandToBoundAgent) {
    SessionRegistry sessions;
    DeviceRegistry registry;
    MockOutbound outbound;
    ManualClock clock;
    CommandService commands{sessions, outbound, clock};
    OperatorService ops{registry, commands};

    // agent 등록 + conn 바인딩(연결됨 상태).
    auto const uuid = make_uuid(0xab);
    auto const& agent = registry.find_or_create(uuid);
    sessions.open(ConnectionId{1});
    sessions.bind(ConnectionId{1}, agent.id, {});

    auto const command_id = ops.set_mode(uuid, ddcs::device::Mode::performance);

    EXPECT_NE(command_id, 0u);
    ASSERT_EQ(outbound.sends.size(), 1u);
    EXPECT_EQ(outbound.sends[0].id, ConnectionId{1});
    EXPECT_EQ(outbound.sends[0].type, static_cast<std::uint8_t>(msg::MessageType::command));

    // Command 봉투 디코드 -> SetMode payload 까지 확인.
    msg::Command sent{};
    auto const& b = outbound.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(b.data()), b.size()}, sent));
    EXPECT_EQ(sent.command_id, command_id);
    EXPECT_EQ(sent.type, static_cast<std::uint8_t>(cmd::CommandType::SetMode));
    cmd::SetMode set_mode{};
    ASSERT_TRUE(cmd::decode({reinterpret_cast<std::byte const*>(sent.payload.data()), sent.payload.size()}, set_mode));
    EXPECT_EQ(set_mode.mode, ddcs::device::Mode::performance);
}

TEST(OperatorServiceTest, SetModeUnknownAgentReturnsZero) {
    SessionRegistry sessions;
    DeviceRegistry registry;
    MockOutbound outbound;
    ManualClock clock;
    CommandService commands{sessions, outbound, clock};
    OperatorService ops{registry, commands};

    auto const command_id = ops.set_mode(make_uuid(0x99), ddcs::device::Mode::safe); // 등록된 적 없음

    EXPECT_EQ(command_id, 0u);
    EXPECT_TRUE(outbound.sends.empty());
}

TEST(OperatorServiceTest, SetModeKnownButOfflineAgentReturnsZero) {
    SessionRegistry sessions;
    DeviceRegistry registry;
    MockOutbound outbound;
    ManualClock clock;
    CommandService commands{sessions, outbound, clock};
    OperatorService ops{registry, commands};

    auto const uuid = make_uuid(0xab);
    (void)registry.find_or_create(uuid); // 등록만, conn 바인딩 없음(미연결)

    auto const command_id = ops.set_mode(uuid, ddcs::device::Mode::normal);

    EXPECT_EQ(command_id, 0u); // resolve 실패 -> dispatch 안 함
    EXPECT_TRUE(outbound.sends.empty());
}
