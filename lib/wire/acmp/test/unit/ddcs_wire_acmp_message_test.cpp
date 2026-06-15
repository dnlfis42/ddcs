#include "ddcs/wire/acmp/message.hpp"

#include "ddcs/common/uuid.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

namespace ddcs::wire::acmp {

namespace {

constexpr std::size_t buf_capacity = 256;

common::Uuid make_uuid() {
    std::array<std::byte, 16> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::byte>(i + 1);
    }
    return common::Uuid{bytes};
}

} // namespace

TEST(AcmpMessageTest, RoundTripsRegisterRequest) {
    std::array<std::byte, buf_capacity> storage{};
    auto const id = make_uuid();
    std::string_view const group{"edge-cluster"};

    auto const written = encode_register_request(id, group, storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::register_request);

    auto const msg = decode_register_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->id, id);
    EXPECT_EQ(msg->group, group);
}

TEST(AcmpMessageTest, RoundTripsRegisterRequestWithEmptyGroup) {
    std::array<std::byte, buf_capacity> storage{};
    auto const id = make_uuid();

    auto const written = encode_register_request(id, {}, storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    auto const msg = decode_register_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->id, id);
    EXPECT_TRUE(msg->group.empty());
}

TEST(AcmpMessageTest, RoundTripsRegisterOutcome) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_register_outcome(0x12, storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::register_outcome);

    auto const msg = decode_register_outcome(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->code, 0x12);
}

TEST(AcmpMessageTest, RoundTripsRegisterAck) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_register_ack(storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::register_ack);
    EXPECT_TRUE(decode_register_ack(in.subspan(1)).has_value());
}

TEST(AcmpMessageTest, RoundTripsHeartbeat) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_heartbeat(storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::heartbeat);
    EXPECT_TRUE(decode_heartbeat(in.subspan(1)).has_value());
}

TEST(AcmpMessageTest, RoundTripsStatus) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_status(2, 0.75, 41.5, storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::status);

    auto const msg = decode_status(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->mode, 2);
    EXPECT_EQ(msg->load, 0.75);
    EXPECT_EQ(msg->temp, 41.5);
}

TEST(AcmpMessageTest, RoundTripsCommandRequestWithPayload) {
    std::array<std::byte, buf_capacity> storage{};
    std::span<std::byte> const out{storage};

    auto const header_written = encode_command_request_header(0x1122334455667788ull, 0x01, out);
    ASSERT_TRUE(header_written.has_value());

    std::array<std::byte, 3> const payload{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}};
    auto tail = out.subspan(*header_written);
    ASSERT_GE(tail.size(), payload.size());
    std::ranges::copy(payload, tail.begin()); // device가 payload를 header 뒤에 기록하는 흐름 모사

    std::span<std::byte const> const in{storage.data(), *header_written + payload.size()};
    EXPECT_EQ(peek_type(in), MessageType::command_request);

    auto const msg = decode_command_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 0x1122334455667788ull);
    EXPECT_EQ(msg->command_type, 0x01);
    ASSERT_EQ(msg->payload.size(), payload.size());
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), msg->payload.begin()));
}

TEST(AcmpMessageTest, RoundTripsCommandRequestWithEmptyPayload) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_command_request_header(7, 0x01, storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::command_request);

    auto const msg = decode_command_request(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 7u);
    EXPECT_EQ(msg->command_type, 0x01);
    EXPECT_TRUE(msg->payload.empty());
}

TEST(AcmpMessageTest, RoundTripsCommandAck) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_command_ack(0xdeadbeefull, storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::command_ack);

    auto const msg = decode_command_ack(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 0xdeadbeefull);
}

TEST(AcmpMessageTest, RoundTripsCommandOutcome) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_command_outcome(42, 0x10, storage);
    ASSERT_TRUE(written.has_value());

    std::span<std::byte const> const in{storage.data(), *written};
    EXPECT_EQ(peek_type(in), MessageType::command_outcome);

    auto const msg = decode_command_outcome(in.subspan(1));
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->command_id, 42u);
    EXPECT_EQ(msg->code, 0x10);
}

TEST(AcmpMessageTest, PeekTypeReturnsInvalidOnEmptyBuffer) {
    std::span<std::byte const> const in{};
    EXPECT_EQ(peek_type(in), MessageType::invalid);
}

TEST(AcmpMessageTest, PeekTypeDoesNotValidateUnknownValue) {
    std::array<std::byte, 1> const raw{std::byte{0x99}}; // 카탈로그에 없는 type
    EXPECT_EQ(static_cast<std::uint8_t>(peek_type(raw)), 0x99u);
}

TEST(AcmpMessageTest, DecodeRejectsTrailingBytes) {
    std::array<std::byte, buf_capacity> storage{};
    auto const written = encode_register_outcome(5, storage);
    ASSERT_TRUE(written.has_value());

    // body 뒤 1바이트 잉여(storage는 0 초기화)
    std::span<std::byte const> const in{storage.data(), *written + 1};
    EXPECT_EQ(peek_type(in), MessageType::register_outcome);
    EXPECT_FALSE(decode_register_outcome(in.subspan(1)).has_value());
}

TEST(AcmpMessageTest, DecodeRejectsTruncatedBody) {
    // command_ack body는 command_id(8)인데 3바이트만 주면 부족.
    std::array<std::byte, 3> const raw{std::byte{1}, std::byte{2}, std::byte{3}};
    EXPECT_FALSE(decode_command_ack(raw).has_value());
}

} // namespace ddcs::wire::acmp
