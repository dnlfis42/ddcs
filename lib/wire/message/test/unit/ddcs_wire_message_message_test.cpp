#include "ddcs/wire/message/message.hpp"

#include "ddcs/common/uuid.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

namespace {

using ddcs::common::Uuid;
using ddcs::wire::message::CommandOutcome;
using ddcs::wire::message::decode_command_ack;
using ddcs::wire::message::decode_command_outcome;
using ddcs::wire::message::decode_command_request;
using ddcs::wire::message::decode_heartbeat;
using ddcs::wire::message::decode_register_ack;
using ddcs::wire::message::decode_register_outcome;
using ddcs::wire::message::decode_register_request;
using ddcs::wire::message::decode_status;
using ddcs::wire::message::encode_command_ack;
using ddcs::wire::message::encode_command_outcome;
using ddcs::wire::message::encode_command_request_header;
using ddcs::wire::message::encode_heartbeat;
using ddcs::wire::message::encode_register_ack;
using ddcs::wire::message::encode_register_outcome;
using ddcs::wire::message::encode_register_request;
using ddcs::wire::message::encode_status;
using ddcs::wire::message::message_type;
using ddcs::wire::message::MessageType;
using ddcs::wire::message::RegisterOutcome;

constexpr std::size_t buf_capacity = 256;

Uuid make_uuid() {
    std::array<std::byte, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(i + 1);
    }
    return Uuid{bytes};
}

TEST(MessageTest, RoundTripsRegisterRequest) {
    std::array<std::byte, buf_capacity> storage{};
    auto const id = make_uuid();
    std::string_view const group{"edge-cluster"};

    auto const written = encode_register_request(storage, id, group);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::register_request);

    auto const msg = decode_register_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->uuid, id);
    EXPECT_EQ(msg->group, group);
}

TEST(MessageTest, RoundTripsRegisterRequestWithEmptyGroup) {
    std::array<std::byte, buf_capacity> storage{};
    auto const id = make_uuid();

    auto const written = encode_register_request(storage, id, {});
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    auto const msg = decode_register_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->uuid, id);
    EXPECT_TRUE(msg->group.empty());
}

TEST(MessageTest, RoundTripsRegisterOutcome) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_register_outcome(storage, RegisterOutcome::Code::failed);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::register_outcome);

    auto const msg = decode_register_outcome(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->code, RegisterOutcome::Code::failed);
}

TEST(MessageTest, RoundTripsRegisterAck) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_register_ack(storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::register_ack);
    EXPECT_TRUE(decode_register_ack(in.subspan(1)).has_value());
}

TEST(MessageTest, RoundTripsHeartbeat) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_heartbeat(storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::heartbeat);
    EXPECT_TRUE(decode_heartbeat(in.subspan(1)).has_value());
}

TEST(MessageTest, RoundTripsStatus) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_status(storage, 2, 0.75, 41.5);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::status);

    auto const msg = decode_status(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->mode, 2);
    EXPECT_EQ(msg->load, 0.75);
    EXPECT_EQ(msg->temp, 41.5);
}

TEST(MessageTest, RoundTripsCommandRequestWithPayload) {
    std::array<std::byte, buf_capacity> storage{};
    std::span<std::byte> const out{storage};

    auto const header_written = encode_command_request_header(out, 0x1122334455667788ull, 0x01);
    ASSERT_TRUE(header_written.has_value());

    std::array<std::byte, 3> const payload{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}};
    auto tail = out.subspan(*header_written);
    ASSERT_GE(tail.size(), payload.size());
    std::ranges::copy(payload, tail.begin()); // device가 payload를 header 뒤에 기록하는 흐름 모사

    std::span<std::byte const> const in{storage.data(), *header_written + payload.size()};
    EXPECT_EQ(message_type(in), MessageType::command_request);

    auto const msg = decode_command_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 0x1122334455667788ull);
    EXPECT_EQ(msg->command_type, 0x01);
    ASSERT_EQ(msg->payload.size(), payload.size());
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), msg->payload.begin()));
}

TEST(MessageTest, RoundTripsCommandRequestWithEmptyPayload) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_command_request_header(storage, 7, 0x01);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::command_request);

    auto const msg = decode_command_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 7u);
    EXPECT_EQ(msg->command_type, 0x01);
    EXPECT_TRUE(msg->payload.empty());
}

TEST(MessageTest, RoundTripsCommandAck) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_command_ack(storage, 0xdeadbeefull);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::command_ack);

    auto const msg = decode_command_ack(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 0xdeadbeefull);
}

TEST(MessageTest, RoundTripsCommandOutcome) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_command_outcome(storage, 42, CommandOutcome::Code::failed);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(message_type(in), MessageType::command_outcome);

    auto const msg = decode_command_outcome(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 42u);
    EXPECT_EQ(msg->code, CommandOutcome::Code::failed);
}

TEST(MessageTest, ReturnsInvalidTypeOnEmptyBuffer) {
    std::span<std::byte const> const in{};
    EXPECT_EQ(message_type(in), MessageType::invalid);
}

TEST(MessageTest, ReturnsRawByteForUnknownType) {
    std::array<std::byte, 1> const raw{std::byte{0x99}}; // 카탈로그에 없는 type
    EXPECT_EQ(static_cast<std::uint8_t>(message_type(raw)), 0x99u);
}

TEST(MessageTest, RejectsTrailingBytes) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_register_outcome(storage, RegisterOutcome::Code::failed);
    ASSERT_TRUE(written.has_value());

    // body 뒤 1바이트 잉여 (storage는 0 초기화)
    std::span<std::byte const> const in{storage.data(), *written + 1};
    EXPECT_EQ(message_type(in), MessageType::register_outcome);
    EXPECT_FALSE(decode_register_outcome(in.subspan(1)).has_value());
}

TEST(MessageTest, RejectsTruncatedBody) {
    // command_ack body는 command_id(8)인데 3바이트만 주면 부족
    std::array<std::byte, 3> const raw{std::byte{1}, std::byte{2}, std::byte{3}};
    EXPECT_FALSE(decode_command_ack(raw).has_value());
}

TEST(MessageTest, EncodeRejectsEmptyOutputBuffer) {
    // 빈 span: type 1바이트도 못 써서 8개 encoder 모두 nullopt
    std::array<std::byte, 0> storage{};
    std::span<std::byte> const out{storage};
    auto const id = make_uuid();
    EXPECT_FALSE(encode_register_request(out, id, "g").has_value());
    EXPECT_FALSE(encode_register_outcome(out, RegisterOutcome::Code::success).has_value());
    EXPECT_FALSE(encode_register_ack(out).has_value());
    EXPECT_FALSE(encode_heartbeat(out).has_value());
    EXPECT_FALSE(encode_status(out, 0, 1.0, 2.0).has_value());
    EXPECT_FALSE(encode_command_request_header(out, 1, 2).has_value());
    EXPECT_FALSE(encode_command_ack(out, 1).has_value());
    EXPECT_FALSE(encode_command_outcome(out, 1, CommandOutcome::Code::success).has_value());
}

TEST(MessageTest, EncodeRejectsTypeOnlyBufferWhenBodyNeeded) {
    // 1바이트: type은 들어가지만 body 직전에 끊긴다. body가 있는 encoder는 nullopt.
    std::array<std::byte, 1> storage{};
    std::span<std::byte> const out{storage};
    auto const id = make_uuid();
    EXPECT_FALSE(encode_register_request(out, id, "g").has_value());
    EXPECT_FALSE(encode_register_outcome(out, RegisterOutcome::Code::success).has_value());
    EXPECT_FALSE(encode_status(out, 0, 1.0, 2.0).has_value());
    EXPECT_FALSE(encode_command_request_header(out, 1, 2).has_value());
    EXPECT_FALSE(encode_command_ack(out, 1).has_value());
    EXPECT_FALSE(encode_command_outcome(out, 1, CommandOutcome::Code::success).has_value());
    // register_ack/heartbeat은 body가 없어 type 1바이트면 성공한다(경계 정상 동작 확인).
    EXPECT_TRUE(encode_register_ack(out).has_value());
    EXPECT_TRUE(encode_heartbeat(out).has_value());
}

} // namespace
