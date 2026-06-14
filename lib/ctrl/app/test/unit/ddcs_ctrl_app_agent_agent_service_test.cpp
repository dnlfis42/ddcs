#include "ddcs/ctrl/app/agent/agent_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/ctrl/app/agent/agent_registry.hpp"
#include "ddcs/ctrl/app/agent/command_sender.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/connection_observer.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/disconnector.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"
#include "ddcs/ctrl/app/agent/port/message_sender.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/register_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/dacp/msg/message.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace msg = ddcs::dacp::msg;

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::ObjectPool;
using ddcs::common::Uuid;
using ddcs::ctrl::app::agent::Agent;
using ddcs::ctrl::app::agent::AgentRegistry;
using ddcs::ctrl::app::agent::AgentService;
using ddcs::ctrl::app::agent::CommandSender;
using ddcs::ctrl::app::agent::port::ConnectionId;
using ddcs::ctrl::app::agent::port::ConnectionObserver;
using ddcs::ctrl::app::agent::port::Disconnector;
using ddcs::ctrl::app::agent::port::DisconnectReason;
using ddcs::ctrl::app::agent::port::MessageBuffer;
using ddcs::ctrl::app::agent::port::MessageSender;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using namespace std::chrono_literals;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return Uuid{bytes};
}

std::span<std::byte const> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

// 송신 기록 대역.
class FakeMessageSender final : public MessageSender {
public:
    struct Sent {
        ConnectionId conn;
        std::uint8_t type;
        std::vector<std::byte> body;
    };

    std::vector<Sent> sent;

    MessageBuffer make_message_buffer() override { return pool_.acquire(); }

    void send(ConnectionId conn, std::uint8_t type, MessageBuffer message) override {
        auto const readable = message->readable();
        sent.push_back(Sent{
            .conn = conn,
            .type = type,
            .body = std::vector<std::byte>{readable.begin(), readable.end()},
        });
    }

private:
    ObjectPool<LinearBuffer> pool_{ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{256})};
};

// infra처럼 disconnect가 동기로 on_disconnected를 되부르는 대역.
class FakeDisconnector final : public Disconnector {
public:
    ConnectionObserver* observer = nullptr; // AgentService 생성 후 연결

    std::vector<ConnectionId> disconnected;

    void disconnect(ConnectionId id) override {
        disconnected.push_back(id);
        if (observer != nullptr) {
            observer->on_disconnected(id, DisconnectReason::local_drop);
        }
    }
};

struct ServiceFixture {
    ManualClock clock;
    AgentRegistry agents;
    FakeMessageSender outbox;
    FakeDisconnector disconnector;
    DeviceRegistry devices;
    ddcs::ctrl::app::device::RegisterService register_service{devices};
    ddcs::ctrl::app::device::StatusService status_service{devices};
    CommandSender command_sender{agents, outbox};
    ddcs::ctrl::app::device::CommandService commands{command_sender, 5s, 1, 500ms};
    AgentService service{agents, outbox, disconnector, clock, register_service, status_service, commands};
    ObjectPool<LinearBuffer> body_pool{ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{256})};

    ServiceFixture() { disconnector.observer = &service; }

    template <typename M>
    MessageBuffer make_body(M const& m) {
        auto buf = body_pool.acquire();
        EXPECT_TRUE(msg::encode(m, *buf));
        return buf;
    }

    template <typename M>
    void deliver(ConnectionId conn, M const& m) {
        service.on_message(conn, static_cast<std::uint8_t>(msg::type_of<M>), make_body(m));
    }

    // 정상 3-way 등록을 끝내고 active 상태로 만든다.
    DeviceId register_active(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        service.on_connected(id);
        deliver(id, msg::RegisterRequest{.id = make_uuid(seed), .group = "g"});
        deliver(id, msg::RegisterAck{});
        EXPECT_NE(agents.find(id), nullptr);
        EXPECT_EQ(agents.find(id)->state(), Agent::State::active);
        return DeviceId{make_uuid(seed)};
    }

    msg::RegisterOutcome last_outcome() {
        msg::RegisterOutcome outcome{};
        EXPECT_FALSE(outbox.sent.empty());
        EXPECT_EQ(outbox.sent.back().type, static_cast<std::uint8_t>(msg::type_of<msg::RegisterOutcome>));
        EXPECT_TRUE(msg::decode(outbox.sent.back().body, outcome));
        return outcome;
    }
};

} // namespace

TEST(AgentServiceTest, ConnectAddsHandshakingAgent) {
    ServiceFixture f;

    f.service.on_connected(ConnectionId{1});

    Agent* const agent = f.agents.find(ConnectionId{1});
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->state(), Agent::State::handshaking);
}

TEST(AgentServiceTest, DisconnectErasesAgent) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    f.service.on_disconnected(ConnectionId{1}, DisconnectReason::peer_closed);

    EXPECT_EQ(f.agents.find(ConnectionId{1}), nullptr);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, RegisterRequestBindsAndSendsSuccessOutcome) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    f.deliver(ConnectionId{1}, msg::RegisterRequest{.id = make_uuid(0xAA), .group = "sensors"});

    Agent* const agent = f.agents.find(ConnectionId{1});
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->state(), Agent::State::confirming); // active는 RegisterAck 이후
    EXPECT_EQ(agent->device(), DeviceId{make_uuid(0xAA)});
    EXPECT_EQ(f.last_outcome().result, msg::RegisterResult::success);
    ASSERT_NE(f.devices.find(DeviceId{make_uuid(0xAA)}), nullptr); // 트윈 생성
    EXPECT_EQ(f.devices.find(DeviceId{make_uuid(0xAA)})->group, "sensors");
}

TEST(AgentServiceTest, RegisterAckActivatesAgent) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});
    f.deliver(ConnectionId{1}, msg::RegisterRequest{.id = make_uuid(0xAA), .group = "g"});

    f.clock.advance(2s);
    f.deliver(ConnectionId{1}, msg::RegisterAck{});

    Agent* const agent = f.agents.find(ConnectionId{1});
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->state(), Agent::State::active);
    EXPECT_EQ(agent->last_seen(), f.clock.now()); // liveness 측정 시작점 = ack 수신
}

TEST(AgentServiceTest, RegisterDecodeFailureDisconnectsWithoutOutcome) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    auto garbage = f.body_pool.acquire();
    ASSERT_TRUE(garbage->write(as_bytes("xx"))); // uuid(16B)에 못 미치는 본문
    f.service.on_message(
        ConnectionId{1}, static_cast<std::uint8_t>(msg::type_of<msg::RegisterRequest>), std::move(garbage)
    );

    EXPECT_TRUE(f.outbox.sent.empty()); // 식별 불가 -> 응답 없음
    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, NilUuidRegisterSendsFailedOutcomeAndDisconnects) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    f.deliver(ConnectionId{1}, msg::RegisterRequest{.id = Uuid{}, .group = "g"});

    EXPECT_EQ(f.last_outcome().result, msg::RegisterResult::failed);
    ASSERT_EQ(f.disconnector.disconnected.size(), 1u); // 등록 실패 -> 판정 송신 후 종료
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, ReregistrationKicksOldConnection) {
    ServiceFixture f;
    DeviceId const device = f.register_active(1, 0xAA);

    f.service.on_connected(ConnectionId{2});
    f.deliver(ConnectionId{2}, msg::RegisterRequest{.id = make_uuid(0xAA), .group = "g"});

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u); // kick-old(new-wins)
    EXPECT_EQ(f.disconnector.disconnected[0], ConnectionId{1});
    EXPECT_EQ(f.agents.find(ConnectionId{1}), nullptr);
    Agent* const fresh = f.agents.find(device);
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(fresh->conn(), ConnectionId{2});
    EXPECT_EQ(fresh->state(), Agent::State::confirming);
    EXPECT_EQ(f.last_outcome().result, msg::RegisterResult::success);
}

TEST(AgentServiceTest, NonRegisterMessageDuringHandshakingKicks) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    f.deliver(ConnectionId{1}, msg::Heartbeat{});

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_TRUE(f.outbox.sent.empty());
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, NonAckMessageDuringConfirmingKicks) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});
    f.deliver(ConnectionId{1}, msg::RegisterRequest{.id = make_uuid(0xAA), .group = "g"});

    f.deliver(ConnectionId{1}, msg::Status{.mode = 1, .load = 0.5, .temp = 40.0});

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u); // 역색인 포함 정리(EraseConfirmingAgentClearsIndex와 짝)
}

TEST(AgentServiceTest, RegisterRequestWhileActiveKicks) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    f.deliver(ConnectionId{1}, msg::RegisterRequest{.id = make_uuid(0xAA), .group = "g"});

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, HeartbeatRefreshesLiveness) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    f.clock.advance(2s);
    f.deliver(ConnectionId{1}, msg::Heartbeat{});

    EXPECT_EQ(f.agents.find(ConnectionId{1})->last_seen(), f.clock.now());
}

TEST(AgentServiceTest, StatusUpdatesTwinAndLiveness) {
    ServiceFixture f;
    DeviceId const device = f.register_active(1, 0xAA);

    f.clock.advance(2s);
    f.deliver(ConnectionId{1}, msg::Status{.mode = 2, .load = 75.5, .temp = 50.25});

    EXPECT_EQ(f.agents.find(ConnectionId{1})->last_seen(), f.clock.now());
    ASSERT_NE(f.devices.find(device), nullptr);
    EXPECT_EQ(f.devices.find(device)->status.mode, ddcs::device::Mode::performance);
    EXPECT_EQ(f.devices.find(device)->status.load, 75.5);
    EXPECT_EQ(f.devices.find(device)->status.temp, 50.25);
}

TEST(AgentServiceTest, CommandAckAndOutcomeSettlePending) {
    ServiceFixture f;
    DeviceId const device = f.register_active(1, 0xAA);

    auto payload = f.commands.make_command_buffer();
    ASSERT_TRUE(payload->write(as_bytes("p")));
    auto const command_id = f.commands.dispatch(device, 0x01, std::move(payload), f.clock.now());
    ASSERT_TRUE(command_id.valid());

    f.deliver(ConnectionId{1}, msg::CommandAck{.command_id = command_id.value()});
    EXPECT_EQ(f.commands.pending_count(), 1u); // ack은 연장만

    f.clock.advance(1s);
    f.deliver(
        ConnectionId{1},
        msg::CommandOutcome{.command_id = command_id.value(), .result = msg::CommandResult::success, .reason = ""}
    );

    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.completed_total(), 1u);
}

TEST(AgentServiceTest, BrokenActivePayloadKicks) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    auto garbage = f.body_pool.acquire();
    ASSERT_TRUE(garbage->write(as_bytes("xx"))); // Status schema(17B)에 못 미치는 본문
    f.service.on_message(ConnectionId{1}, static_cast<std::uint8_t>(msg::type_of<msg::Status>), std::move(garbage));

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, UnknownTypeWhileActiveKicks) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    f.service.on_message(ConnectionId{1}, 0x7F, f.body_pool.acquire());

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, MessageFromUnknownConnectionIgnored) {
    ServiceFixture f;

    f.deliver(ConnectionId{9}, msg::Heartbeat{}); // add된 적 없는 conn

    EXPECT_TRUE(f.disconnector.disconnected.empty());
    EXPECT_TRUE(f.outbox.sent.empty());
}
