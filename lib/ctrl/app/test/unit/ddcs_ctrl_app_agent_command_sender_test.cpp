#include "ddcs/ctrl/app/agent/command_sender.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/agent/agent.hpp"
#include "ddcs/ctrl/app/agent/agent_registry.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"
#include "ddcs/ctrl/app/agent/port/message_sender.hpp"
#include "ddcs/ctrl/app/device/port/command_buffer.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/wire/acmp/message.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace acmp = ddcs::wire::acmp;
namespace frame = ddcs::wire::frame;

using ddcs::common::ManualClock;
using ddcs::ctrl::app::agent::AgentRegistry;
using ddcs::ctrl::app::agent::CommandSender;
using ddcs::ctrl::app::agent::port::ConnectionId;
using ddcs::ctrl::app::agent::port::MessageBuffer;
using ddcs::ctrl::app::agent::port::MessageSender;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::domain::DeviceId;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

std::span<std::byte const> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

// 송신된 command_request를 decode해 기록하는 대역. frame 헤더 자리가 보존됐는지도 검사한다.
class FakeMessageSender final : public MessageSender {
public:
    struct Sent {
        ConnectionId conn;
        std::uint64_t command_id;
        std::uint8_t type;
        std::string payload;
        bool frame_headroom_ok;
    };

    std::vector<Sent> sent;

    MessageBuffer make_message_buffer() override {
        auto message = pool_.acquire();
        EXPECT_TRUE(message->reserve_front(frame::header_size)); // infra 계약 모사
        return message;
    }

    void send(ConnectionId conn, MessageBuffer message) override {
        //  // [type][command_id][command_type][payload]
        auto const bytes = message->readable();
        ASSERT_FALSE(bytes.empty());
        EXPECT_EQ(acmp::peek_type(bytes), acmp::MessageType::command_request);
        auto const cmd = acmp::decode_command_request(bytes.subspan(1));
        ASSERT_TRUE(cmd.has_value());
        std::string payload_copy{
            reinterpret_cast<char const*>(cmd->payload.data()), cmd->payload.size()
        };
        std::array<std::byte, frame::header_size> const frame_stub{}; // frame 헤더 자리 검증
        sent.push_back(
            Sent{
                .conn = conn,
                .command_id = cmd->command_id,
                .type = cmd->command_type,
                .payload = std::move(payload_copy),
                .frame_headroom_ok = message->write_front(frame_stub),
            }
        );
    }

private:
    ddcs::common::ObjectPool<ddcs::common::LinearBuffer> pool_{
        ddcs::common::make_object_pool<ddcs::common::LinearBuffer>(0, 4, std::size_t{128})
    };
};

struct SenderFixture {
    ManualClock clock;
    AgentRegistry agents;
    FakeMessageSender outbox;
    CommandSender sender{agents, outbox};

    ConnectionId activate(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        EXPECT_TRUE(agents.add(id, clock.now()));
        EXPECT_TRUE(agents.bind(id, make_device_id(seed), clock.now()));
        EXPECT_TRUE(agents.find(id)->confirm(clock.now()));
        return id;
    }

    ddcs::ctrl::app::device::port::CommandBuffer payload(std::string_view s) {
        auto buf = sender.make_command_buffer();
        EXPECT_TRUE(buf->write(as_bytes(s)));
        return buf;
    }
};

} // namespace

TEST(CommandSenderTest, SendsEncodedCommandToActiveConnection) {
    SenderFixture f;
    ConnectionId const conn = f.activate(1, 0xAA);

    EXPECT_TRUE(f.sender.try_send(make_device_id(0xAA), CommandId{42}, 0x01, f.payload("payload")));

    ASSERT_EQ(f.outbox.sent.size(), 1u);
    EXPECT_EQ(f.outbox.sent[0].conn, conn);
    EXPECT_EQ(f.outbox.sent[0].command_id, 42u);
    EXPECT_EQ(f.outbox.sent[0].type, 0x01);
    EXPECT_EQ(f.outbox.sent[0].payload, "payload");
    EXPECT_TRUE(f.outbox.sent[0].frame_headroom_ok); // command 헤더 기록 후에도 frame 자리 보존
}

TEST(CommandSenderTest, ReturnsFalseWhenDeviceUnknown) {
    SenderFixture f;

    EXPECT_FALSE(f.sender.try_send(make_device_id(0xAA), CommandId{42}, 0x01, f.payload("p")));
    EXPECT_TRUE(f.outbox.sent.empty());
}

TEST(CommandSenderTest, ReturnsFalseWhenDeviceNotActive) {
    SenderFixture f;
    ASSERT_TRUE(f.agents.add(ConnectionId{1}, f.clock.now())); // handshaking. bind 전

    EXPECT_FALSE(f.sender.try_send(make_device_id(0xAA), CommandId{42}, 0x01, f.payload("p")));
    EXPECT_TRUE(f.outbox.sent.empty());
}

TEST(CommandSenderTest, ReturnsFalseWhenDeviceConfirming) {
    SenderFixture f;
    ASSERT_TRUE(f.agents.add(ConnectionId{1}, f.clock.now()));
    // RegisterAck 전
    ASSERT_TRUE(f.agents.bind(ConnectionId{1}, make_device_id(0xAA), f.clock.now()));

    EXPECT_FALSE(f.sender.try_send(make_device_id(0xAA), CommandId{42}, 0x01, f.payload("p")));
    EXPECT_TRUE(f.outbox.sent.empty()); // 등록 미확인 연결에는 명령 금지
}

TEST(CommandSenderTest, ReturnsFalseWithoutHeaderHeadroom) {
    SenderFixture f;
    f.activate(1, 0xAA);

    // make_command_buffer를 거치지 않은 buffer. command headroom 없음
    auto raw = f.outbox.make_message_buffer();
    ASSERT_TRUE(raw->write(as_bytes("p")));

    EXPECT_FALSE(f.sender.try_send(make_device_id(0xAA), CommandId{42}, 0x01, std::move(raw)));
    EXPECT_TRUE(f.outbox.sent.empty());
}
