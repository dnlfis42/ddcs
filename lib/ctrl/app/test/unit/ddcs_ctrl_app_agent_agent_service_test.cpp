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
#include "ddcs/wire/acmp/message.hpp"

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

namespace acmp = ddcs::wire::acmp;

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

std::uint8_t type_byte(acmp::MessageType type) {
    return static_cast<std::uint8_t>(type);
}

// 송신 기록 대역. payload(acmp `[type][body]`)를 통째로 보관한다.
class FakeMessageSender final : public MessageSender {
public:
    struct Sent {
        ConnectionId conn;
        std::vector<std::byte> payload;
    };

    std::vector<Sent> sent;

    MessageBuffer make_message_buffer() override {
        return pool_.acquire();
    }

    void send(ConnectionId conn, MessageBuffer message) override {
        auto const readable = message->data_span();
        sent.push_back(Sent{
            .conn = conn,
            .payload = std::vector<std::byte>{readable.begin(), readable.end()},
        });
    }

private:
    ObjectPool<LinearBuffer> pool_{ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{256
    })};
};

// infra처럼 disconnect가 동기로 on_disconnected를 되부르는 대역
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
    AgentService service{agents,           outbox,         disconnector, clock,
                         register_service, status_service, commands};
    ObjectPool<LinearBuffer> body_pool{
        ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{256})
    };

    ServiceFixture() {
        disconnector.observer = &service;
    }

    // 타입별 acmp payload(`[type][body]`) 생성
    MessageBuffer payload_register_request(Uuid const& id, std::string_view group) {
        auto buf = body_pool.acquire();
        auto const w = acmp::encode_register_request(buf->tailroom_span(), id, group);
        EXPECT_TRUE(w.has_value());
        if (w) {
            EXPECT_TRUE(buf->try_commit(*w));
        }
        return buf;
    }
    MessageBuffer payload_register_ack() {
        auto buf = body_pool.acquire();
        auto const w = acmp::encode_register_ack(buf->tailroom_span());
        EXPECT_TRUE(w.has_value());
        if (w) {
            EXPECT_TRUE(buf->try_commit(*w));
        }
        return buf;
    }
    MessageBuffer payload_heartbeat() {
        auto buf = body_pool.acquire();
        auto const w = acmp::encode_heartbeat(buf->tailroom_span());
        EXPECT_TRUE(w.has_value());
        if (w) {
            EXPECT_TRUE(buf->try_commit(*w));
        }
        return buf;
    }
    MessageBuffer payload_status(std::uint8_t mode, double load, double temp) {
        auto buf = body_pool.acquire();
        auto const w = acmp::encode_status(buf->tailroom_span(), mode, load, temp);
        EXPECT_TRUE(w.has_value());
        if (w) {
            EXPECT_TRUE(buf->try_commit(*w));
        }
        return buf;
    }
    MessageBuffer payload_command_ack(std::uint64_t command_id) {
        auto buf = body_pool.acquire();
        auto const w = acmp::encode_command_ack(buf->tailroom_span(), command_id);
        EXPECT_TRUE(w.has_value());
        if (w) {
            EXPECT_TRUE(buf->try_commit(*w));
        }
        return buf;
    }
    MessageBuffer
    payload_command_outcome(std::uint64_t command_id, acmp::CommandOutcome::Code code) {
        auto buf = body_pool.acquire();
        auto const w = acmp::encode_command_outcome(buf->tailroom_span(), command_id, code);
        EXPECT_TRUE(w.has_value());
        if (w) {
            EXPECT_TRUE(buf->try_commit(*w));
        }
        return buf;
    }
    // 임의 type + body로 조립 (decode 실패/미지 type 경로 검증용)
    MessageBuffer payload_raw(std::uint8_t type, std::string_view body) {
        auto buf = body_pool.acquire();
        std::array<std::byte, 1> const t{std::byte{type}};
        EXPECT_TRUE(buf->try_append(t));
        if (!body.empty()) {
            EXPECT_TRUE(buf->try_append(as_bytes(body)));
        }
        return buf;
    }

    void deliver(ConnectionId conn, MessageBuffer payload) {
        service.on_message(conn, std::move(payload));
    }

    // 정상 3-way 등록을 끝내고 active 상태로 만든다.
    DeviceId register_active(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        service.on_connected(id);
        deliver(id, payload_register_request(make_uuid(seed), "g"));
        deliver(id, payload_register_ack());
        EXPECT_NE(agents.find(id), nullptr);
        EXPECT_EQ(agents.find(id)->state(), Agent::State::active);
        return DeviceId{make_uuid(seed)};
    }

    // 마지막으로 송신된 RegisterOutcome의 code.
    acmp::RegisterOutcome::Code last_outcome_code() {
        EXPECT_FALSE(outbox.sent.empty());
        auto const& p = outbox.sent.back().payload;
        std::span<std::byte const> const bytes{p.data(), p.size()};
        EXPECT_EQ(acmp::message_type(bytes), acmp::MessageType::register_outcome);
        auto const outcome = acmp::decode_register_outcome(bytes.subspan(1));
        EXPECT_TRUE(outcome.has_value());
        return outcome ? outcome->code : acmp::RegisterOutcome::Code::failed;
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

    f.deliver(ConnectionId{1}, f.payload_register_request(make_uuid(0xAA), "sensors"));

    Agent* const agent = f.agents.find(ConnectionId{1});
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->state(), Agent::State::confirming); // active는 RegisterAck 이후
    EXPECT_EQ(agent->device(), DeviceId{make_uuid(0xAA)});
    EXPECT_EQ(f.last_outcome_code(), acmp::RegisterOutcome::Code::success);
    ASSERT_NE(f.devices.find(DeviceId{make_uuid(0xAA)}), nullptr); // 트윈 생성
    EXPECT_EQ(f.devices.find(DeviceId{make_uuid(0xAA)})->group, "sensors");
}

TEST(AgentServiceTest, RegisterAckActivatesAgent) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});
    f.deliver(ConnectionId{1}, f.payload_register_request(make_uuid(0xAA), "g"));

    f.clock.advance(2s);
    f.deliver(ConnectionId{1}, f.payload_register_ack());

    Agent* const agent = f.agents.find(ConnectionId{1});
    ASSERT_NE(agent, nullptr);
    EXPECT_EQ(agent->state(), Agent::State::active);
    EXPECT_EQ(agent->last_seen(), f.clock.now()); // liveness 측정 시작점 = ack 수신
}

TEST(AgentServiceTest, RegisterDecodeFailureDisconnectsWithoutOutcome) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    // uuid(16B)에 못 미치는 본문
    f.deliver(ConnectionId{1}, f.payload_raw(type_byte(acmp::MessageType::register_request), "xx"));

    EXPECT_TRUE(f.outbox.sent.empty()); // 식별 불가라서 응답 없음
    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, NilUuidRegisterSendsFailedOutcomeAndDisconnects) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    f.deliver(ConnectionId{1}, f.payload_register_request(Uuid{}, "g"));

    EXPECT_EQ(f.last_outcome_code(), acmp::RegisterOutcome::Code::failed);
    ASSERT_EQ(f.disconnector.disconnected.size(), 1u); // 등록 실패 시 판정 송신 후 종료
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, ReregistrationKicksOldConnection) {
    ServiceFixture f;
    DeviceId const device = f.register_active(1, 0xAA);

    f.service.on_connected(ConnectionId{2});
    f.deliver(ConnectionId{2}, f.payload_register_request(make_uuid(0xAA), "g"));

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u); // kick-old(new-wins)
    EXPECT_EQ(f.disconnector.disconnected[0], ConnectionId{1});
    EXPECT_EQ(f.agents.find(ConnectionId{1}), nullptr);
    Agent* const fresh = f.agents.find(device);
    ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(fresh->conn(), ConnectionId{2});
    EXPECT_EQ(fresh->state(), Agent::State::confirming);
    EXPECT_EQ(f.last_outcome_code(), acmp::RegisterOutcome::Code::success);
}

TEST(AgentServiceTest, NonRegisterMessageDuringHandshakingKicks) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});

    f.deliver(ConnectionId{1}, f.payload_heartbeat());

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_TRUE(f.outbox.sent.empty());
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, NonAckMessageDuringConfirmingKicks) {
    ServiceFixture f;
    f.service.on_connected(ConnectionId{1});
    f.deliver(ConnectionId{1}, f.payload_register_request(make_uuid(0xAA), "g"));

    f.deliver(ConnectionId{1}, f.payload_status(1, 0.5, 40.0));

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u); // 역색인 포함 정리(EraseConfirmingAgentClearsIndex와 짝)
}

TEST(AgentServiceTest, RegisterRequestWhileActiveKicks) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    f.deliver(ConnectionId{1}, f.payload_register_request(make_uuid(0xAA), "g"));

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, HeartbeatRefreshesLiveness) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    f.clock.advance(2s);
    f.deliver(ConnectionId{1}, f.payload_heartbeat());

    EXPECT_EQ(f.agents.find(ConnectionId{1})->last_seen(), f.clock.now());
}

TEST(AgentServiceTest, StatusUpdatesTwinAndLiveness) {
    ServiceFixture f;
    DeviceId const device = f.register_active(1, 0xAA);

    f.clock.advance(2s);
    f.deliver(ConnectionId{1}, f.payload_status(2, 75.5, 50.25));

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
    ASSERT_TRUE(payload->try_append(as_bytes("p")));
    auto const command_id = f.commands.dispatch(device, 0x01, std::move(payload), f.clock.now());
    ASSERT_TRUE(command_id.valid());

    f.deliver(ConnectionId{1}, f.payload_command_ack(command_id.get()));
    EXPECT_EQ(f.commands.pending_count(), 1u); // ack은 연장만

    f.clock.advance(1s);
    f.deliver(
        ConnectionId{1},
        f.payload_command_outcome(command_id.get(), acmp::CommandOutcome::Code::success)
    );

    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.completed_total(), 1u);
}

TEST(AgentServiceTest, BrokenActivePayloadKicks) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    // Status schema(17B)에 못 미치는 본문
    f.deliver(ConnectionId{1}, f.payload_raw(type_byte(acmp::MessageType::status), "xx"));

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, UnknownTypeWhileActiveKicks) {
    ServiceFixture f;
    f.register_active(1, 0xAA);

    f.deliver(ConnectionId{1}, f.payload_raw(0x7F, ""));

    ASSERT_EQ(f.disconnector.disconnected.size(), 1u);
    EXPECT_EQ(f.agents.size(), 0u);
}

TEST(AgentServiceTest, MessageFromUnknownConnectionIgnored) {
    ServiceFixture f;

    f.deliver(ConnectionId{9}, f.payload_heartbeat()); // add된 적 없는 conn

    EXPECT_TRUE(f.disconnector.disconnected.empty());
    EXPECT_TRUE(f.outbox.sent.empty());
}
