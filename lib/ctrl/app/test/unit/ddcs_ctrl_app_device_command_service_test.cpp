#include "ddcs/ctrl/app/device/command_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/port/command.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/device/mode.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::ctrl::app::device::CommandService;
using ddcs::ctrl::app::device::port::Command;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::CommandSender;
using ddcs::ctrl::app::device::port::SetMode;
using ddcs::ctrl::domain::DeviceId;
using ddcs::device::Mode;
using namespace std::chrono_literals;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

Mode sent_mode(Command const& command) {
    return std::get<SetMode>(command).mode;
}

// 송신 의뢰를 기록하는 대역. accept = false면 송신 거부 (미연결 등가)
class FakeCommandSender final : public CommandSender {
public:
    struct Sent {
        DeviceId device;
        CommandId command_id;
        Command command;
    };

    std::vector<Sent> sent;
    bool accept = true;

    bool try_send(DeviceId device, CommandId command_id, Command const& command) override {
        if (!accept) {
            return false;
        }
        sent.push_back(Sent{.device = device, .command_id = command_id, .command = command});
        return true;
    }
};

struct CommandFixture {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 1, 500ms};

    CommandId send(Mode mode, std::uint8_t seed = 0xAA) {
        return commands.dispatch(make_device_id(seed), SetMode{.mode = mode}, clock.now());
    }
};

TEST(CommandServiceTest, DispatchSendsCommandAndRegistersSlot) {
    CommandFixture f;

    auto const id = f.send(Mode::performance);

    ASSERT_TRUE(id.valid());
    EXPECT_EQ(f.commands.pending_count(), 1u);
    EXPECT_EQ(f.commands.metrics().dispatched_total, 1u);
    ASSERT_EQ(f.sender.sent.size(), 1u);
    EXPECT_EQ(f.sender.sent[0].device, make_device_id(0xAA));
    EXPECT_EQ(f.sender.sent[0].command_id, id);
    EXPECT_EQ(sent_mode(f.sender.sent[0].command), Mode::performance);
}

TEST(CommandServiceTest, DispatchReturnsInvalidWhenSendRejected) {
    CommandFixture f;
    f.sender.accept = false;

    auto const id = f.send(Mode::performance);

    EXPECT_FALSE(id.valid());
    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.metrics().dispatched_total, 0u);
}

TEST(CommandServiceTest, SupersedeReplacesSameFamilyCommand) {
    CommandFixture f;
    auto const old_id = f.send(Mode::performance);

    auto const new_id = f.send(Mode::normal); // 같은 device, 같은 계열

    EXPECT_EQ(f.commands.metrics().superseded_total, 1u);
    EXPECT_EQ(f.commands.pending_count(), 1u); // 슬롯은 교체, 누적 아님
    EXPECT_NE(new_id, old_id);

    // 대체된 명령의 늦은 응답
    f.commands.settle(make_device_id(0xAA), old_id, true, "", f.clock.now());
    EXPECT_EQ(f.commands.metrics().completed_total, 0u);
    EXPECT_EQ(f.commands.metrics().stale_total, 1u);

    f.commands.settle(make_device_id(0xAA), new_id, true, "", f.clock.now());
    EXPECT_EQ(f.commands.metrics().completed_total, 1u);
    EXPECT_EQ(f.commands.pending_count(), 0u);
}

TEST(CommandServiceTest, SupersedeAppliesEvenWhenSendFails) {
    CommandFixture f;
    ASSERT_TRUE(f.send(Mode::performance).valid());

    f.sender.accept = false;
    auto const new_id = f.send(Mode::normal);

    EXPECT_FALSE(new_id.valid());
    EXPECT_EQ(f.commands.metrics().superseded_total, 1u); // 의도 폐기는 송신 성패와 무관
    EXPECT_EQ(f.commands.pending_count(), 0u);            // 옛 슬롯 닫힘 + 새 슬롯 없음
}

TEST(CommandServiceTest, AcknowledgeExtendsDeadline) {
    CommandFixture f;
    auto const id = f.send(Mode::performance);

    f.clock.advance(4s);
    f.commands.acknowledge(make_device_id(0xAA), id, f.clock.now()); // deadline = 4s + 5s

    f.clock.advance(2s); // 최초 deadline(5s)은 지났지만 연장 deadline(9s) 전
    f.commands.sweep(f.clock.now());

    EXPECT_EQ(f.commands.metrics().timed_out_total, 0u);
    EXPECT_EQ(f.commands.pending_count(), 1u);
}

TEST(CommandServiceTest, SettleSuccessClosesSlotAndRecordsRtt) {
    CommandFixture f;
    auto const id = f.send(Mode::performance);

    f.clock.advance(2s);
    f.commands.settle(make_device_id(0xAA), id, true, "", f.clock.now());

    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.metrics().completed_total, 1u);
    EXPECT_EQ(f.commands.metrics().rtt_ms_sum, 2000u);
}

TEST(CommandServiceTest, SettleFailureGivesUpWhenNoRetry) {
    CommandFixture f;
    auto const id = f.send(Mode::performance);

    f.commands.settle(make_device_id(0xAA), id, false, "busy", f.clock.now());

    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.metrics().gave_up_total, 1u);
    EXPECT_EQ(f.commands.metrics().completed_total, 0u);
}

TEST(CommandServiceTest, StaleResponseCountedQuietly) {
    CommandFixture f;
    ASSERT_TRUE(f.send(Mode::performance).valid());

    f.commands.acknowledge(make_device_id(0xAA), CommandId{999}, f.clock.now());
    f.commands.settle(make_device_id(0xAA), CommandId{999}, true, "", f.clock.now());

    EXPECT_EQ(f.commands.metrics().stale_total, 2u); // 정상 부산물. 카운터로만 관측
    EXPECT_EQ(f.commands.pending_count(), 1u);
    EXPECT_EQ(f.commands.metrics().completed_total, 0u);
}

TEST(CommandServiceTest, SweepTimeoutGivesUpWhenNoRetry) {
    CommandFixture f;
    ASSERT_TRUE(f.send(Mode::performance).valid());

    f.clock.advance(6s);
    f.commands.sweep(f.clock.now());

    EXPECT_EQ(f.commands.metrics().timed_out_total, 1u);
    EXPECT_EQ(f.commands.metrics().gave_up_total, 1u);
    EXPECT_EQ(f.commands.pending_count(), 0u);
}

TEST(CommandServiceTest, RetryResendsSameIdWithRetainedCommand) {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 2, 500ms}; // 재시도 1회 허용

    auto const id =
        commands.dispatch(make_device_id(0xAA), SetMode{.mode = Mode::performance}, clock.now());
    ASSERT_TRUE(id.valid());

    clock.advance(6s);
    commands.sweep(clock.now()); // timeout 후 backoff
    EXPECT_EQ(commands.metrics().timed_out_total, 1u);
    EXPECT_EQ(commands.pending_count(), 1u);

    clock.advance(1s); // backoff(500ms) 경과
    commands.sweep(clock.now());

    EXPECT_EQ(commands.metrics().retried_total, 1u);
    ASSERT_EQ(sender.sent.size(), 2u);
    EXPECT_EQ(sender.sent[1].command_id, id);                        // 재전송은 동일 id
    EXPECT_EQ(sent_mode(sender.sent[1].command), Mode::performance); // 보관본에서 복원
    EXPECT_EQ(sender.sent[1].device, make_device_id(0xAA));

    commands.settle(make_device_id(0xAA), id, true, "", clock.now());
    EXPECT_EQ(commands.pending_count(), 0u);
    EXPECT_EQ(commands.metrics().completed_total, 1u);
}

TEST(CommandServiceTest, RetryGivesUpWhenSendRejected) {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 2, 500ms};

    ASSERT_TRUE(commands
                    .dispatch(make_device_id(0xAA), SetMode{.mode = Mode::performance}, clock.now())
                    .valid());

    clock.advance(6s);
    commands.sweep(clock.now()); // timeout 후 backoff
    sender.accept = false;       // 끊긴 뒤 재접속 없음 등가

    clock.advance(1s);
    commands.sweep(clock.now()); // 재전송 시도 시 거부되어 포기

    EXPECT_EQ(commands.metrics().gave_up_total, 1u);
    EXPECT_EQ(commands.pending_count(), 0u);
    EXPECT_EQ(commands.metrics().retried_total, 0u);
}

// NACK(settle success=false)인데 재시도 예산이 남으면 포기하지 말고 backoff 후 재전송해야 한다.
// 기존 NACK 테스트는 전부 max_attempts=1이라 곧장 gave_up으로 단락되어 이 분기를 못 본다(timeout이
// 아니라 NACK이 fail_attempt를 거쳐 재시도로 가는 경로).
TEST(CommandServiceTest, NackWithRetryBudgetResendsInsteadOfGivingUp) {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 2, 500ms}; // 재시도 1회 허용

    auto const id =
        commands.dispatch(make_device_id(0xAA), SetMode{.mode = Mode::performance}, clock.now());
    ASSERT_TRUE(id.valid());

    // NACK: 예산이 남았으니 포기(gave_up) 대신 backoff 대기로 전환한다.
    commands.settle(make_device_id(0xAA), id, false, "busy", clock.now());
    EXPECT_EQ(commands.metrics().gave_up_total, 0u); // 핵심: NACK이라도 예산 있으면 포기 X
    EXPECT_EQ(commands.pending_count(), 1u);         // 여전히 미결(backoff 대기)
    EXPECT_EQ(sender.sent.size(), 1u);               // 아직 재전송 전

    clock.advance(600ms); // backoff(500ms) 경과
    commands.sweep(clock.now());

    EXPECT_EQ(commands.metrics().retried_total, 1u); // backoff 후 동일 id 재전송
    ASSERT_EQ(sender.sent.size(), 2u);
    EXPECT_EQ(sender.sent[1].command_id, id);
    EXPECT_EQ(sent_mode(sender.sent[1].command), Mode::performance); // 보관본에서 복원
}

// 현실 설정(max_attempts=3)에서 예산을 모두 소진하면(재시도 2회 후) 포기한다. 또한 backoff가
// attempt마다 2배로 자라는지(500ms -> 1000ms) 동일한 600ms 프로브로 확인한다.
TEST(CommandServiceTest, ExhaustsRetryBudgetThenGivesUpWithBackoffDoubling) {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 3, 500ms}; // 재시도 2회 (attempt 1 -> 2 -> 3)

    auto const id =
        commands.dispatch(make_device_id(0xAA), SetMode{.mode = Mode::performance}, clock.now());
    ASSERT_TRUE(id.valid());

    // attempt 1 timeout -> backoff_for(1) = 500ms
    clock.advance(6s);
    commands.sweep(clock.now());
    EXPECT_EQ(commands.metrics().timed_out_total, 1u);

    // 600ms 프로브: attempt 1 backoff(500ms)는 경과 -> 재전송(attempt 2).
    clock.advance(600ms);
    commands.sweep(clock.now());
    EXPECT_EQ(commands.metrics().retried_total, 1u);
    ASSERT_EQ(sender.sent.size(), 2u);

    // attempt 2 timeout -> backoff_for(2) = 1000ms (2배)
    clock.advance(6s);
    commands.sweep(clock.now());
    EXPECT_EQ(commands.metrics().timed_out_total, 2u);

    // 동일한 600ms 프로브: 이번엔 backoff가 1000ms라 아직 미경과 -> 재전송 없음(= 2배로 자랐다는
    // 증거).
    clock.advance(600ms);
    commands.sweep(clock.now());
    EXPECT_EQ(commands.metrics().retried_total, 1u);
    EXPECT_EQ(sender.sent.size(), 2u);

    // 추가 600ms(누적 1200ms > 1000ms) -> 재전송(attempt 3)
    clock.advance(600ms);
    commands.sweep(clock.now());
    EXPECT_EQ(commands.metrics().retried_total, 2u);
    ASSERT_EQ(sender.sent.size(), 3u);

    // attempt 3 timeout -> 예산 소진(attempts 3 >= max 3) -> 포기
    clock.advance(6s);
    commands.sweep(clock.now());
    EXPECT_EQ(commands.metrics().timed_out_total, 3u);
    EXPECT_EQ(commands.metrics().gave_up_total, 1u);
    EXPECT_EQ(commands.pending_count(), 0u);
}

} // namespace
