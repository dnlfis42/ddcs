#include "ddcs/agent/app/session/session_service.hpp"

#include "ddcs/agent/app/transport/port/outbound.hpp"
#include "ddcs/agent/app/transport/port/timer_slot.hpp"
#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/wire/message/command.hpp"
#include "ddcs/wire/message/message.hpp"

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

namespace msg = ddcs::wire::message;

using ddcs::agent::app::session::SessionService;
using ddcs::agent::app::transport::port::Outbound;
using ddcs::agent::app::transport::port::TimerSlot;
using ddcs::agent::domain::Device;
using ddcs::agent::domain::DummyDevice;
using ddcs::agent::domain::Status;
using ddcs::common::LinearBuffer;
using ddcs::common::ObjectPool;
using ddcs::common::PoolHandle;
using ddcs::common::Uuid;
using ddcs::device::decode_mode;
using ddcs::device::Mode;

// 송신 기록 대역
// - payload를 통째로 보관한다(infra가 frame header를 덧씌우기 전 상태).
class MockOutbound : public Outbound {
public:
    PoolHandle<LinearBuffer> payload_buffer() override {
        return pool.acquire();
    }

    void send(PoolHandle<LinearBuffer> message) override {
        auto const r = message->data_span();
        sends.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }

    void schedule_timer(TimerSlot id, std::chrono::nanoseconds d) override {
        timers.emplace_back(id, d);
    }

    void cancel_timer(TimerSlot id) override {
        cancels.push_back(id);
    }

    void close() override {
        ++closes;
    }

    void notify_registered() override {
        ++registered_notifications;
    }

    ObjectPool<LinearBuffer> pool{
        ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{1024})
    };

    std::vector<std::string> sends; // 각 원소가 msg payload (`[type][body]`)
    std::vector<std::pair<TimerSlot, std::chrono::nanoseconds>> timers;
    std::vector<TimerSlot> cancels;
    int closes = 0;
    int registered_notifications = 0;
};

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b.fill(std::byte{seed});
    return Uuid{b};
}

ObjectPool<LinearBuffer>& build_pool() {
    static auto pool = ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{256});
    return pool;
}

// 수신 payload 빌더
PoolHandle<LinearBuffer> payload_register_outcome(msg::RegisterOutcome::Code code) {
    auto buf = build_pool().acquire();
    auto const w = msg::encode_register_outcome(buf->tailroom_span(), code);
    EXPECT_TRUE(w.has_value());
    if (w) {
        EXPECT_TRUE(buf->try_commit(*w));
    }
    return buf;
}
PoolHandle<LinearBuffer> payload_heartbeat() {
    auto buf = build_pool().acquire();
    auto const w = msg::encode_heartbeat(buf->tailroom_span());
    EXPECT_TRUE(w.has_value());
    if (w) {
        EXPECT_TRUE(buf->try_commit(*w));
    }
    return buf;
}
// command_request: `[type][command_id][command_type]` 헤더 뒤에 device payload를 append
PoolHandle<LinearBuffer>
payload_command(std::uint64_t id, std::uint8_t command_type, std::span<std::byte const> cmd) {
    auto buf = build_pool().acquire();
    auto const w = msg::encode_command_request_header(buf->tailroom_span(), id, command_type);
    EXPECT_TRUE(w.has_value());
    if (w) {
        EXPECT_TRUE(buf->try_commit(*w));
    }
    if (!cmd.empty()) {
        EXPECT_TRUE(buf->try_append(cmd));
    }
    return buf;
}
PoolHandle<LinearBuffer> setmode_command(std::uint64_t id, Mode mode) {
    static auto cmd_pool = ddcs::common::ObjectPool<LinearBuffer>::create<8>(std::size_t{64});
    auto cmd_buf = cmd_pool.acquire();
    auto const w = msg::encode_set_mode(cmd_buf->tailroom_span(), static_cast<std::uint8_t>(mode));
    EXPECT_TRUE(w.has_value());
    if (w) {
        EXPECT_TRUE(cmd_buf->try_commit(*w));
    }
    return payload_command(
        id, static_cast<std::uint8_t>(msg::CommandType::set_mode), cmd_buf->data_span()
    );
}

msg::MessageType sent_type(std::string const& s) {
    return msg::message_type({reinterpret_cast<std::byte const*>(s.data()), s.size()});
}
std::span<std::byte const> sent_body(std::string const& s) {
    std::span<std::byte const> const bytes{reinterpret_cast<std::byte const*>(s.data()), s.size()};
    return bytes.subspan(1);
}

bool has_timer(MockOutbound const& o, TimerSlot id) {
    for (auto const& [t, d] : o.timers) {
        if (t == id) {
            return true;
        }
    }
    return false;
}

bool has_cancel(MockOutbound const& o, TimerSlot id) {
    for (auto const c : o.cancels) {
        if (c == id) {
            return true;
        }
    }
    return false;
}

int count_timer(MockOutbound const& o, TimerSlot id) {
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
    svc.on_recv(payload_register_outcome(msg::RegisterOutcome::Code::success));
    out.sends.clear();
    out.timers.clear();
    out.cancels.clear();
}

TEST(AgentSessionServiceTest, OnConnectedSendsRegisterAndArmsTimeout) {
    DummyDevice device{make_uuid(0xab)};
    MockOutbound out;
    SessionService svc{device, out};

    svc.on_connected();

    EXPECT_EQ(svc.state(), SessionService::State::registering);
    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::register_request);
    auto const req = msg::decode_register_request(sent_body(out.sends[0]));
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->uuid, make_uuid(0xab));
    EXPECT_TRUE(has_timer(out, TimerSlot::register_timeout));
}

TEST(AgentSessionServiceTest, RegisterRequestCarriesConfiguredGroup) {
    DummyDevice device;
    MockOutbound out;
    SessionService::Config cfg{};
    cfg.group = "sensors";
    SessionService svc{device, out, cfg};

    svc.on_connected();

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::register_request);
    auto const req = msg::decode_register_request(sent_body(out.sends[0]));
    ASSERT_TRUE(req.has_value());
    EXPECT_EQ(req->group, "sensors");
}

TEST(AgentSessionServiceTest, RegisterOutcomeSuccessSendsAckAndEntersActive) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected();

    svc.on_recv(payload_register_outcome(msg::RegisterOutcome::Code::success));

    EXPECT_EQ(svc.state(), SessionService::State::active);
    EXPECT_TRUE(has_cancel(out, TimerSlot::register_timeout));
    EXPECT_EQ(out.closes, 0);
    EXPECT_EQ(out.registered_notifications, 1); // 등록 성공 시 transport backoff 리셋 통지
    // 3-way(register_request->register_ack) 후 enter_active가 초기 status를 즉시 게시
    ASSERT_EQ(out.sends.size(), 3u);
    EXPECT_EQ(sent_type(out.sends[1]), msg::MessageType::register_ack);
    EXPECT_EQ(sent_type(out.sends[2]), msg::MessageType::status);
}

TEST(AgentSessionServiceTest, EnterActiveSendsInitialStatusReport) {
    DummyDevice device{{}, Mode::performance};
    device.set_load(42);
    device.set_temp(30);
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected();

    svc.on_recv(payload_register_outcome(msg::RegisterOutcome::Code::success));

    // 등록 직후 초기 텔레메트리 1건: register_request, register_ack, status
    ASSERT_EQ(out.sends.size(), 3u);
    EXPECT_EQ(sent_type(out.sends[2]), msg::MessageType::status);
    auto const st = msg::decode_status(sent_body(out.sends[2]));
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(decode_mode(st->mode), Mode::performance); // 디바이스 실제 상태를 게시
    EXPECT_DOUBLE_EQ(st->load, 42.0);
}

TEST(AgentSessionServiceTest, RegisterOutcomeFailedCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected();

    svc.on_recv(payload_register_outcome(msg::RegisterOutcome::Code::failed));

    EXPECT_NE(svc.state(), SessionService::State::active);
    EXPECT_EQ(out.closes, 1);
    EXPECT_EQ(out.registered_notifications, 0); // 거부면 backoff 리셋 통지 없음
}

TEST(AgentSessionServiceTest, RegisterTimeoutClosesConnection) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected();

    svc.on_timer(TimerSlot::register_timeout);

    EXPECT_EQ(svc.state(), SessionService::State::closing);
    EXPECT_EQ(out.closes, 1);
    EXPECT_EQ(out.registered_notifications, 0); // 미응답 timeout이면 backoff 리셋 통지 없음
}

TEST(AgentSessionServiceTest, UnexpectedTypeWhileRegisteringCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected();

    svc.on_recv(payload_heartbeat());

    EXPECT_EQ(out.closes, 1);
}

TEST(AgentSessionServiceTest, DisconnectResetsToIdle) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected();

    svc.on_disconnected();

    EXPECT_EQ(svc.state(), SessionService::State::idle);
    EXPECT_TRUE(has_cancel(out, TimerSlot::register_timeout));
}

TEST(AgentSessionServiceTest, EnterActiveArmsHeartbeatAndStatusTimers) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected();

    svc.on_recv(payload_register_outcome(msg::RegisterOutcome::Code::success));

    EXPECT_TRUE(has_timer(out, TimerSlot::heartbeat));
    EXPECT_TRUE(has_timer(out, TimerSlot::status));
}

TEST(AgentSessionServiceTest, HeartbeatTimerSendsHeartbeatAndReschedules) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_timer(TimerSlot::heartbeat);

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::heartbeat);
    EXPECT_EQ(count_timer(out, TimerSlot::heartbeat), 1); // 재무장
}

TEST(AgentSessionServiceTest, StatusTimerSendsStatusFromDeviceAndReschedules) {
    DummyDevice device{{}, Mode::performance};
    device.set_load(80);
    device.set_temp(55);
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_timer(TimerSlot::status);

    ASSERT_EQ(out.sends.size(), 1u);
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::status);
    auto const st = msg::decode_status(sent_body(out.sends[0]));
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(decode_mode(st->mode), Mode::performance); // mode 반영 (어휘 계약 라운드트립)
    EXPECT_DOUBLE_EQ(st->load, 80.0);                    // load 반영
    EXPECT_DOUBLE_EQ(st->temp, 55.0);                    // temp 반영
    EXPECT_EQ(count_timer(out, TimerSlot::status), 1);   // 재무장
}

TEST(AgentSessionServiceTest, HeartbeatTimerIgnoredWhenNotActive) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    svc.on_connected(); // registering, not active

    svc.on_timer(TimerSlot::heartbeat);

    for (auto const& s : out.sends) {
        EXPECT_NE(sent_type(s), msg::MessageType::heartbeat);
    }
}

TEST(AgentSessionServiceTest, CommandAppliesToDeviceAndAcksThenOutcomes) {
    DummyDevice device{{}, Mode::safe};
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_recv(setmode_command(1, Mode::performance));

    EXPECT_EQ(device.mode(), Mode::performance); // device 적용
    ASSERT_EQ(out.sends.size(), 2u);
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::command_ack); // ACK 먼저
    EXPECT_EQ(sent_type(out.sends[1]), msg::MessageType::command_outcome);

    auto const ack = msg::decode_command_ack(sent_body(out.sends[0]));
    ASSERT_TRUE(ack.has_value());
    EXPECT_EQ(ack->command_id, 1u);

    auto const outcome = msg::decode_command_outcome(sent_body(out.sends[1]));
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(outcome->command_id, 1u);
    EXPECT_EQ(outcome->code, msg::CommandOutcome::Code::success);
}

TEST(AgentSessionServiceTest, DuplicateCommandResendsWithoutReapplying) {
    DummyDevice device{{}, Mode::safe};
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_recv(setmode_command(1, Mode::performance));
    ASSERT_EQ(device.mode(), Mode::performance);
    out.sends.clear();

    // 같은 command_id=1로 다른 mode면 dedup: 재적용 안 함(device 유지) + 응답만 재송신
    svc.on_recv(setmode_command(1, Mode::safe));

    EXPECT_EQ(device.mode(), Mode::performance); // 재적용 안 됨
    ASSERT_EQ(out.sends.size(), 2u);             // ACK+Outcome 재송신
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::command_ack);
    EXPECT_EQ(sent_type(out.sends[1]), msg::MessageType::command_outcome);
}

TEST(AgentSessionServiceTest, UnknownCommandTypeOutcomesFailed) {
    DummyDevice device{{}, Mode::safe};
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_recv(payload_command(5, 0xFF, {})); // 미지 CommandType

    EXPECT_EQ(device.mode(), Mode::safe); // device 변동 없음
    ASSERT_EQ(out.sends.size(), 2u);
    auto const outcome = msg::decode_command_outcome(sent_body(out.sends[1]));
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(outcome->code, msg::CommandOutcome::Code::failed);
}

TEST(AgentSessionServiceTest, OutOfVocabularyModeOutcomesFailedWithoutApplying) {
    DummyDevice device{{}, Mode::safe};
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    // 구조는 유효한 set_mode지만 mode byte가 어휘 밖(0xFF)이다.
    std::array<std::byte, 1> const bad_mode{std::byte{0xFF}};
    svc.on_recv(
        payload_command(7, static_cast<std::uint8_t>(msg::CommandType::set_mode), bad_mode)
    );

    EXPECT_EQ(device.mode(), Mode::safe); // 적용 안 됨
    ASSERT_EQ(out.sends.size(), 2u);      // decode 성공이라 ACK은 나가고 outcome은 실패
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::command_ack);
    auto const outcome = msg::decode_command_outcome(sent_body(out.sends[1]));
    ASSERT_TRUE(outcome.has_value());
    EXPECT_EQ(outcome->code, msg::CommandOutcome::Code::failed);
}

TEST(AgentSessionServiceTest, UnexpectedTypeWhileActiveCloses) {
    DummyDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_recv(payload_heartbeat());

    EXPECT_EQ(out.closes, 1);
}

// apply()가 항상 실패하는 Device 대역. DummyDevice는 항상 성공이라 apply 실패 분기를 못 탄다.
class FailingDevice : public Device {
public:
    Uuid id() const override {
        return make_uuid(0x11);
    }
    Status query() override {
        return Status{Mode::safe, 0.0, 0.0};
    }
    bool apply(Mode /*mode*/) override {
        return false; // 적용 실패
    }
};

TEST(AgentSessionServiceTest, ApplyFailureOutcomesFailed) {
    FailingDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_recv(setmode_command(1, Mode::performance));

    ASSERT_EQ(out.sends.size(), 2u);
    // decode 성공이라 ACK은 나간다
    EXPECT_EQ(sent_type(out.sends[0]), msg::MessageType::command_ack);
    auto const outcome = msg::decode_command_outcome(sent_body(out.sends[1]));
    ASSERT_TRUE(outcome.has_value());
    // apply 실패 -> failed
    EXPECT_EQ(outcome->code, msg::CommandOutcome::Code::failed);
}

TEST(AgentSessionServiceTest, ApplyFailureDedupResendsFailed) {
    FailingDevice device;
    MockOutbound out;
    SessionService svc{device, out};
    activate(svc, out);

    svc.on_recv(setmode_command(1, Mode::performance));
    out.sends.clear();

    // 같은 command_id 재전송 -> dedup: apply 재시도 없이 보존된 failed outcome을 재송신
    svc.on_recv(setmode_command(1, Mode::performance));

    ASSERT_EQ(out.sends.size(), 2u);
    auto const outcome = msg::decode_command_outcome(sent_body(out.sends[1]));
    ASSERT_TRUE(outcome.has_value());
    // last_command_code_ 보존 확인
    EXPECT_EQ(outcome->code, msg::CommandOutcome::Code::failed);
}

} // namespace
