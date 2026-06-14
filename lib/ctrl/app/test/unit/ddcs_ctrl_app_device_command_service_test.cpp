#include "ddcs/ctrl/app/device/command_service.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/device/port/command_buffer.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/device/port/command_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::ManualClock;
using ddcs::common::ObjectPool;
using ddcs::ctrl::app::device::CommandService;
using ddcs::ctrl::app::device::port::CommandBuffer;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::CommandSender;
using ddcs::ctrl::domain::DeviceId;
using namespace std::chrono_literals;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

std::span<std::byte const> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

// 송신 의뢰를 기록하는 대역. accept = false면 송신 거부(미연결 등가).
class FakeCommandSender final : public CommandSender {
public:
    struct Sent {
        DeviceId device;
        CommandId command_id;
        std::uint8_t type;
        std::string payload;
    };

    std::vector<Sent> sent;
    bool accept = true;

    CommandBuffer make_command_buffer() override { return pool_.acquire(); }

    bool try_send(DeviceId device, CommandId command_id, std::uint8_t command_type, CommandBuffer message) override {
        if (!accept) {
            return false;
        }
        auto const readable = message->readable();
        sent.push_back(Sent{
            .device = device,
            .command_id = command_id,
            .type = command_type,
            .payload = std::string{reinterpret_cast<char const*>(readable.data()), readable.size()},
        });
        return true;
    }

private:
    ObjectPool<LinearBuffer> pool_{ddcs::common::make_object_pool<LinearBuffer>(0, 8, std::size_t{128})};
};

struct CommandFixture {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 1, 500ms};

    CommandId send(std::uint8_t type, std::string_view payload, std::uint8_t seed = 0xAA) {
        auto buf = commands.make_command_buffer();
        EXPECT_TRUE(buf->write(as_bytes(payload)));
        return commands.dispatch(make_device_id(seed), type, std::move(buf), clock.now());
    }
};

} // namespace

TEST(CommandServiceTest, DispatchSendsCommandAndRegistersSlot) {
    CommandFixture f;

    auto const id = f.send(0x01, "payload");

    ASSERT_TRUE(id.valid());
    EXPECT_EQ(f.commands.pending_count(), 1u);
    EXPECT_EQ(f.commands.dispatched_total(), 1u);
    ASSERT_EQ(f.sender.sent.size(), 1u);
    EXPECT_EQ(f.sender.sent[0].device, make_device_id(0xAA));
    EXPECT_EQ(f.sender.sent[0].command_id, id);
    EXPECT_EQ(f.sender.sent[0].type, 0x01);
    EXPECT_EQ(f.sender.sent[0].payload, "payload");
}

TEST(CommandServiceTest, DispatchReturnsInvalidWhenSendRejected) {
    CommandFixture f;
    f.sender.accept = false;

    auto const id = f.send(0x01, "p");

    EXPECT_FALSE(id.valid());
    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.dispatched_total(), 0u);
}

TEST(CommandServiceTest, SupersedeReplacesSameTypeCommand) {
    CommandFixture f;
    auto const old_id = f.send(0x01, "old");

    auto const new_id = f.send(0x01, "new"); // 같은 device, 같은 계열

    EXPECT_EQ(f.commands.superseded_total(), 1u);
    EXPECT_EQ(f.commands.pending_count(), 1u); // 슬롯은 교체, 누적 아님
    EXPECT_NE(new_id, old_id);

    f.commands.settle(make_device_id(0xAA), old_id, true, "", f.clock.now()); // 대체된 명령의 늦은 응답
    EXPECT_EQ(f.commands.completed_total(), 0u);
    EXPECT_EQ(f.commands.stale_total(), 1u);

    f.commands.settle(make_device_id(0xAA), new_id, true, "", f.clock.now());
    EXPECT_EQ(f.commands.completed_total(), 1u);
    EXPECT_EQ(f.commands.pending_count(), 0u);
}

TEST(CommandServiceTest, DifferentCommandTypesCoexist) {
    CommandFixture f;
    auto const mode_id = f.send(0x01, "mode");
    auto const other_id = f.send(0x02, "other");

    EXPECT_EQ(f.commands.superseded_total(), 0u); // 다른 계열은 이웃
    EXPECT_EQ(f.commands.pending_count(), 2u);

    f.commands.settle(make_device_id(0xAA), mode_id, true, "", f.clock.now());
    EXPECT_EQ(f.commands.pending_count(), 1u); // other는 영향 없음

    f.commands.settle(make_device_id(0xAA), other_id, true, "", f.clock.now());
    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.completed_total(), 2u);
}

TEST(CommandServiceTest, SupersedeAppliesEvenWhenSendFails) {
    CommandFixture f;
    ASSERT_TRUE(f.send(0x01, "old").valid());

    f.sender.accept = false;
    auto const new_id = f.send(0x01, "new");

    EXPECT_FALSE(new_id.valid());
    EXPECT_EQ(f.commands.superseded_total(), 1u); // 의도 폐기는 송신 성패와 무관
    EXPECT_EQ(f.commands.pending_count(), 0u);    // 옛 슬롯 닫힘 + 새 슬롯 없음
}

TEST(CommandServiceTest, AcknowledgeExtendsDeadline) {
    CommandFixture f;
    auto const id = f.send(0x01, "p");

    f.clock.advance(4s);
    f.commands.acknowledge(make_device_id(0xAA), id, f.clock.now()); // deadline = 4s + 5s

    f.clock.advance(2s); // 최초 deadline(5s)은 지났지만 연장 deadline(9s) 전
    f.commands.sweep(f.clock.now());

    EXPECT_EQ(f.commands.timed_out_total(), 0u);
    EXPECT_EQ(f.commands.pending_count(), 1u);
}

TEST(CommandServiceTest, SettleSuccessClosesSlotAndRecordsRtt) {
    CommandFixture f;
    auto const id = f.send(0x01, "p");

    f.clock.advance(2s);
    f.commands.settle(make_device_id(0xAA), id, true, "", f.clock.now());

    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.completed_total(), 1u);
    EXPECT_EQ(f.commands.rtt_ms_sum(), 2000u);
}

TEST(CommandServiceTest, SettleFailureGivesUpWhenNoRetry) {
    CommandFixture f;
    auto const id = f.send(0x01, "p");

    f.commands.settle(make_device_id(0xAA), id, false, "busy", f.clock.now());

    EXPECT_EQ(f.commands.pending_count(), 0u);
    EXPECT_EQ(f.commands.gave_up_total(), 1u);
    EXPECT_EQ(f.commands.completed_total(), 0u);
}

TEST(CommandServiceTest, StaleResponseCountedQuietly) {
    CommandFixture f;
    ASSERT_TRUE(f.send(0x01, "p").valid());

    f.commands.acknowledge(make_device_id(0xAA), CommandId{999}, f.clock.now());
    f.commands.settle(make_device_id(0xAA), CommandId{999}, true, "", f.clock.now());

    EXPECT_EQ(f.commands.stale_total(), 2u); // 정상 부산물. 카운터로만 관측
    EXPECT_EQ(f.commands.pending_count(), 1u);
    EXPECT_EQ(f.commands.completed_total(), 0u);
}

TEST(CommandServiceTest, SweepTimeoutGivesUpWhenNoRetry) {
    CommandFixture f;
    ASSERT_TRUE(f.send(0x01, "p").valid());

    f.clock.advance(6s);
    f.commands.sweep(f.clock.now());

    EXPECT_EQ(f.commands.timed_out_total(), 1u);
    EXPECT_EQ(f.commands.gave_up_total(), 1u);
    EXPECT_EQ(f.commands.pending_count(), 0u);
}

TEST(CommandServiceTest, RetryResendsSameIdWithRetainedPayload) {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 2, 500ms}; // 재시도 1회 허용

    auto buf = commands.make_command_buffer();
    ASSERT_TRUE(buf->write(as_bytes("p")));
    auto const id = commands.dispatch(make_device_id(0xAA), 0x01, std::move(buf), clock.now());
    ASSERT_TRUE(id.valid());

    clock.advance(6s);
    commands.sweep(clock.now()); // timeout -> backoff
    EXPECT_EQ(commands.timed_out_total(), 1u);
    EXPECT_EQ(commands.pending_count(), 1u);

    clock.advance(1s); // backoff(500ms) 경과
    commands.sweep(clock.now());

    EXPECT_EQ(commands.retried_total(), 1u);
    ASSERT_EQ(sender.sent.size(), 2u);
    EXPECT_EQ(sender.sent[1].command_id, id); // 재전송은 동일 id
    EXPECT_EQ(sender.sent[1].payload, "p");   // 보관본에서 복원
    EXPECT_EQ(sender.sent[1].device, make_device_id(0xAA));

    commands.settle(make_device_id(0xAA), id, true, "", clock.now());
    EXPECT_EQ(commands.pending_count(), 0u);
    EXPECT_EQ(commands.completed_total(), 1u);
}

TEST(CommandServiceTest, RetryGivesUpWhenSendRejected) {
    ManualClock clock;
    FakeCommandSender sender;
    CommandService commands{sender, 5s, 2, 500ms};

    auto buf = commands.make_command_buffer();
    ASSERT_TRUE(buf->write(as_bytes("p")));
    ASSERT_TRUE(commands.dispatch(make_device_id(0xAA), 0x01, std::move(buf), clock.now()).valid());

    clock.advance(6s);
    commands.sweep(clock.now()); // timeout -> backoff
    sender.accept = false;       // 끊긴 뒤 재접속 없음 등가

    clock.advance(1s);
    commands.sweep(clock.now()); // 재전송 시도 -> 거부 포기

    EXPECT_EQ(commands.gave_up_total(), 1u);
    EXPECT_EQ(commands.pending_count(), 0u);
    EXPECT_EQ(commands.retried_total(), 0u);
}
