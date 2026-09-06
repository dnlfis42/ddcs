#include "ddcs/ctrl/app/session/command_sender.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/device/port/command_id.hpp"
#include "ddcs/ctrl/app/session/session.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/message_buffer.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/wire/command/command.hpp"
#include "ddcs/wire/frame/frame.hpp"
#include "ddcs/wire/message/message.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace msg = ddcs::wire::message;
namespace cmd = ddcs::wire::command;
namespace frame = ddcs::wire::frame;

using ddcs::common::ManualClock;
using ddcs::ctrl::app::device::port::CommandId;
using ddcs::ctrl::app::device::port::SendResult;
using ddcs::ctrl::app::session::CommandSender;
using ddcs::ctrl::app::session::SessionRegistry;
using ddcs::ctrl::app::transport::port::ConnectionId;
using ddcs::ctrl::app::transport::port::MessageBuffer;
using ddcs::ctrl::app::transport::port::MessageSender;
using ddcs::ctrl::domain::DeviceId;
using ddcs::device::Mode;
using ddcs::wire::command::SetMode;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

// 송신된 command_request를 디코딩해 기록하는 대역. frame 헤더 자리가 보존됐는지도 검사한다.
class FakeMessageSender final : public MessageSender {
public:
    struct Sent {
        ConnectionId conn;
        std::uint64_t command_id;
        std::uint8_t type;
        Mode mode;
        bool frame_headroom_ok;
    };

    std::vector<Sent> sent;

    MessageBuffer make_message_buffer() override {
        auto message = pool_.acquire();
        EXPECT_TRUE(message->grow_headroom(frame::header_size)); // infra 계약 모사
        return message;
    }

    void send(ConnectionId conn, MessageBuffer message) override {
        //  // [type][command_id][command_type][payload]
        auto const decoded = msg::decode_message(message->data_span());
        ASSERT_TRUE(decoded.has_value());
        auto const* req = std::get_if<msg::CommandRequest>(&*decoded);
        ASSERT_NE(req, nullptr);
        auto const set_mode = cmd::decode_set_mode(req->payload);
        ASSERT_TRUE(set_mode.has_value());
        std::array<std::byte, frame::header_size> const frame_stub{}; // frame 헤더 자리 검증
        sent.push_back(
            Sent{
                .conn = conn,
                .command_id = req->command_id,
                .type = req->command_type,
                .mode = ddcs::device::decode_mode(set_mode->mode).value_or(Mode::safe),
                .frame_headroom_ok = message->prepend(frame_stub),
            }
        );
    }

private:
    ddcs::common::ObjectPool<ddcs::common::LinearBuffer> pool_{
        ddcs::common::ObjectPool<ddcs::common::LinearBuffer>::create<4>(std::size_t{128})
    };
};

struct SenderFixture {
    ManualClock clock;
    SessionRegistry sessions;
    FakeMessageSender outbox;
    CommandSender sender{sessions, outbox};

    ConnectionId activate(std::uint64_t conn, std::uint8_t seed) {
        ConnectionId const id{conn};
        EXPECT_TRUE(sessions.add(id, clock.now()));
        EXPECT_TRUE(sessions.bind(id, make_device_id(seed), clock.now()));
        EXPECT_TRUE(sessions.find(id)->confirm(clock.now()));
        return id;
    }
};

TEST(CommandSenderTest, SendsEncodedCommandToActiveConnection) {
    SenderFixture f;
    ConnectionId const conn = f.activate(1, 0xAA);

    EXPECT_EQ(
        f.sender.send(
            make_device_id(0xAA), CommandId{42},
            SetMode{.mode = ddcs::device::encode_mode(Mode::performance)}
        ),
        SendResult::ok
    );

    ASSERT_EQ(f.outbox.sent.size(), 1u);
    EXPECT_EQ(f.outbox.sent[0].conn, conn);
    EXPECT_EQ(f.outbox.sent[0].command_id, 42u);
    EXPECT_EQ(f.outbox.sent[0].type, static_cast<std::uint8_t>(cmd::CommandType::set_mode));
    EXPECT_EQ(f.outbox.sent[0].mode, Mode::performance);
    EXPECT_TRUE(f.outbox.sent[0].frame_headroom_ok); // command 헤더 기록 후에도 frame 자리 보존
}

TEST(CommandSenderTest, ReturnsOfflineWhenDeviceUnknown) {
    SenderFixture f;

    EXPECT_EQ(
        f.sender.send(
            make_device_id(0xAA), CommandId{42},
            SetMode{.mode = ddcs::device::encode_mode(Mode::normal)}
        ),
        SendResult::offline
    );
    EXPECT_TRUE(f.outbox.sent.empty());
}

TEST(CommandSenderTest, ReturnsOfflineWhenDeviceNotActive) {
    SenderFixture f;
    ASSERT_TRUE(f.sessions.add(ConnectionId{1}, f.clock.now())); // handshaking. bind 전

    EXPECT_EQ(
        f.sender.send(
            make_device_id(0xAA), CommandId{42},
            SetMode{.mode = ddcs::device::encode_mode(Mode::normal)}
        ),
        SendResult::offline
    );
    EXPECT_TRUE(f.outbox.sent.empty());
}

TEST(CommandSenderTest, ReturnsOfflineWhenDeviceConfirming) {
    SenderFixture f;
    ASSERT_TRUE(f.sessions.add(ConnectionId{1}, f.clock.now()));
    // RegisterAck 전
    ASSERT_TRUE(f.sessions.bind(ConnectionId{1}, make_device_id(0xAA), f.clock.now()));

    EXPECT_EQ(
        f.sender.send(
            make_device_id(0xAA), CommandId{42},
            SetMode{.mode = ddcs::device::encode_mode(Mode::normal)}
        ),
        SendResult::offline
    );
    EXPECT_TRUE(f.outbox.sent.empty()); // 등록 미확인 연결에는 명령 금지
}

} // namespace
