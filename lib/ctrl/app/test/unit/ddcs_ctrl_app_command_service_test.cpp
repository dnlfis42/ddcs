#include "ddcs/ctrl/app/agent/command_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
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
using ddcs::ctrl::app::agent::CommandService;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Outbound;
namespace msg = ddcs::proto::msg;

DeviceId make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return DeviceId{b};
}

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

PoolHandle<LinearBuffer> make_ack_body(std::uint64_t command_id) {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{64});
    auto buf = pool.acquire();
    msg::CommandAck const ack{.command_id = command_id};
    EXPECT_TRUE(msg::encode(ack, *buf));
    return buf;
}

PoolHandle<LinearBuffer> make_outcome_body(std::uint64_t command_id, msg::CommandResult result) {
    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 8, std::size_t{64});
    auto buf = pool.acquire();
    msg::CommandOutcome const out{.command_id = command_id, .result = result, .reason = {}};
    EXPECT_TRUE(msg::encode(out, *buf));
    return buf;
}

// agent 를 conn 에 바인딩(active)해 dispatch 가 resolve 할 수 있게 한다.
void bind_agent(SessionRegistry& sessions, ConnectionId conn, DeviceId agent) {
    sessions.open(conn);
    sessions.bind(conn, agent, {});
}

constexpr std::chrono::seconds kTimeout{5};

} // namespace

TEST(CommandServiceTest, DispatchSendsCommandAndTracksPending) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    auto const command_id = svc.dispatch(make_uuid(1), 0x01, "payload");

    EXPECT_NE(command_id, 0u);
    EXPECT_EQ(svc.pending_count(), 1u);
    EXPECT_EQ(svc.dispatched_total(), 1u);
    ASSERT_EQ(outbound.sends.size(), 1u);
    EXPECT_EQ(outbound.sends[0].id, ConnectionId{1});
    EXPECT_EQ(outbound.sends[0].type, static_cast<std::uint8_t>(msg::MessageType::command));

    msg::Command sent{};
    auto const& b = outbound.sends[0].body;
    ASSERT_TRUE(msg::decode({reinterpret_cast<std::byte const*>(b.data()), b.size()}, sent));
    EXPECT_EQ(sent.command_id, command_id);
    EXPECT_EQ(sent.type, 0x01);
    EXPECT_EQ(sent.payload, "payload");
}

TEST(CommandServiceTest, DispatchOfflineAgentReturnsInvalid) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};

    auto const command_id = svc.dispatch(make_uuid(1), 0x01, "payload"); // 미바인딩

    EXPECT_EQ(command_id, 0u);
    EXPECT_EQ(svc.pending_count(), 0u);
    EXPECT_TRUE(outbound.sends.empty());
}

TEST(CommandServiceTest, OutcomeResolvesPending) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    auto const command_id = svc.dispatch(make_uuid(1), 0x01, "payload");
    svc.handle_outcome(ConnectionId{1}, make_outcome_body(command_id, msg::CommandResult::success));

    EXPECT_EQ(svc.pending_count(), 0u); // 결과 확정 -> 종료
}

TEST(CommandServiceTest, SweepExpiresTimedOutCommand) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    svc.dispatch(make_uuid(1), 0x01, "payload");
    clock.advance(kTimeout + std::chrono::seconds{1}); // deadline 초과
    svc.sweep();

    EXPECT_EQ(svc.pending_count(), 0u);
    EXPECT_EQ(svc.timed_out_total(), 1u);
}

TEST(CommandServiceTest, OutcomeRecordsRoundTripTime) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    auto const command_id = svc.dispatch(make_uuid(1), 0x01, "payload");
    clock.advance(std::chrono::milliseconds{120});
    svc.handle_outcome(ConnectionId{1}, make_outcome_body(command_id, msg::CommandResult::success));

    EXPECT_EQ(svc.completed_total(), 1u);
    EXPECT_EQ(svc.rtt_ms_sum(), 120u); // dispatch->outcome 120ms
}

TEST(CommandServiceTest, AckExtendsDeadline) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    auto const command_id = svc.dispatch(make_uuid(1), 0x01, "payload"); // deadline = t0 + 5s
    clock.advance(std::chrono::seconds{4});                             // t0 + 4s
    svc.handle_ack(ConnectionId{1}, make_ack_body(command_id));         // deadline = t0 + 9s 로 연장

    clock.advance(std::chrono::seconds{4}); // t0 + 8s (원 deadline 초과지만 연장됨)
    svc.sweep();
    EXPECT_EQ(svc.pending_count(), 1u); // 아직 미만료

    clock.advance(std::chrono::seconds{2}); // t0 + 10s (연장 deadline 초과)
    svc.sweep();
    EXPECT_EQ(svc.pending_count(), 0u);
}

TEST(CommandServiceTest, OutcomeFromWrongConnIgnored) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    auto const command_id = svc.dispatch(make_uuid(1), 0x01, "payload");
    svc.handle_outcome(ConnectionId{2}, make_outcome_body(command_id, msg::CommandResult::success)); // 남의 conn

    EXPECT_EQ(svc.pending_count(), 1u); // 무시 -> 미결 유지
}

TEST(CommandServiceTest, OutcomeForUnknownCommandIgnored) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    svc.dispatch(make_uuid(1), 0x01, "payload");
    svc.handle_outcome(ConnectionId{1}, make_outcome_body(9999, msg::CommandResult::success)); // 미발급 id

    EXPECT_EQ(svc.pending_count(), 1u); // 기존 미결 영향 없음
}

TEST(CommandServiceTest, AckDecodeFailDropped) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    svc.dispatch(make_uuid(1), 0x01, "payload");

    static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 4, std::size_t{64});
    auto bad = pool.acquire();
    std::array<std::byte, 3> junk{}; // command_id(8B) 미달 -> decode 실패
    ASSERT_TRUE(bad->write({junk.data(), junk.size()}));
    svc.handle_ack(ConnectionId{1}, std::move(bad));

    EXPECT_EQ(svc.pending_count(), 1u); // 비치명적 -> 미결 유지
}

// --- 부분실패 보상 (재시도/백오프) -------------------------------------------
TEST(CommandServiceTest, RetriesOnTimeoutAfterBackoff) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout, 3, std::chrono::seconds{1}}; // max=3, backoff 1s
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    svc.dispatch(make_uuid(1), 0x01, "payload"); // attempt 1
    ASSERT_EQ(outbound.sends.size(), 1u);

    clock.advance(kTimeout + std::chrono::seconds{1}); // 응답 timeout
    svc.sweep();                                       // in_flight timeout -> backoff(재발송 대기)
    EXPECT_EQ(svc.timed_out_total(), 1u);
    EXPECT_EQ(svc.pending_count(), 1u);   // 살아있음(backoff)
    EXPECT_EQ(outbound.sends.size(), 1u); // 아직 재발송 안 함

    clock.advance(std::chrono::seconds{2}); // backoff(1s) 경과
    svc.sweep();                            // redispatch(새 command_id)
    EXPECT_EQ(svc.retried_total(), 1u);
    EXPECT_EQ(outbound.sends.size(), 2u); // 재발송
    EXPECT_EQ(svc.pending_count(), 1u);
}

TEST(CommandServiceTest, RetriesOnNack) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout, 3, std::chrono::seconds{1}};
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    auto const id = svc.dispatch(make_uuid(1), 0x01, "payload");
    svc.handle_outcome(ConnectionId{1}, make_outcome_body(id, msg::CommandResult::failed)); // NACK

    EXPECT_EQ(svc.completed_total(), 0u); // NACK 은 완료 아님
    EXPECT_EQ(svc.pending_count(), 1u);   // backoff (드롭 안 함)

    clock.advance(std::chrono::seconds{2}); // backoff 경과
    svc.sweep();
    EXPECT_EQ(svc.retried_total(), 1u);
    EXPECT_EQ(outbound.sends.size(), 2u);
}

TEST(CommandServiceTest, GivesUpAfterMaxAttempts) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout, 2, std::chrono::milliseconds{100}}; // max=2
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    svc.dispatch(make_uuid(1), 0x01, "payload");        // attempt 1
    clock.advance(kTimeout + std::chrono::seconds{1}); // timeout
    svc.sweep();                                       // -> backoff
    clock.advance(std::chrono::seconds{1});            // backoff 경과
    svc.sweep();                                       // -> redispatch(attempt 2)
    EXPECT_EQ(svc.retried_total(), 1u);
    EXPECT_EQ(svc.pending_count(), 1u);

    clock.advance(kTimeout + std::chrono::seconds{1}); // attempt 2 timeout
    svc.sweep();                                       // attempts(2) >= max -> 포기
    EXPECT_EQ(svc.gave_up_total(), 1u);
    EXPECT_EQ(svc.pending_count(), 0u);
}

TEST(CommandServiceTest, LateSuccessOutcomeCompletesDuringBackoff) {
    SessionRegistry sessions;
    MockOutbound outbound;
    ManualClock clock;
    CommandService svc{sessions, outbound, clock, kTimeout, 3, std::chrono::seconds{10}}; // 긴 backoff
    bind_agent(sessions, ConnectionId{1}, make_uuid(1));

    auto const id = svc.dispatch(make_uuid(1), 0x01, "payload");
    clock.advance(kTimeout + std::chrono::seconds{1}); // timeout
    svc.sweep();                                       // -> backoff (원 command_id 유지)
    EXPECT_EQ(svc.pending_count(), 1u);

    svc.handle_outcome(ConnectionId{1}, make_outcome_body(id, msg::CommandResult::success)); // 늦은 성공
    EXPECT_EQ(svc.completed_total(), 1u);
    EXPECT_EQ(svc.pending_count(), 0u); // 완료 -> 재시도 취소
}
