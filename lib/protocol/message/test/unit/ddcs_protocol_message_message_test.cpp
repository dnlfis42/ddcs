#include "ddcs/protocol/message/message.hpp"

#include <gtest/gtest.h>

#include <array>

namespace ddcs::protocol::message {

TEST(MessageTest, RoundTripsRegisterRequest) {
    RegisterRequest const in{.agent_tag = "agent-001"};
    auto const decoded = decode_register_request(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsRegisterSuccess) {
    RegisterSuccess const in{};
    auto const decoded = decode_register_success(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsRegisterFail) {
    RegisterFail const in{.reason = "tag already taken"};
    auto const decoded = decode_register_fail(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsStatus) {
    Status const in{
        .timestamp_ns = 1'234'567'890'000ULL,
        .state = R"({"agent":"ok","device":"online"})",
    };
    auto const decoded = decode_status(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsCommand) {
    Command const in{
        .command_id = 0xDEADBEEF12345678ULL,
        .body = R"({"action":"reboot","args":{"delay_s":30}})",
    };
    auto const decoded = decode_command(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsCommandAck) {
    CommandAck const in{.command_id = 42};
    auto const decoded = decode_command_ack(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsCommandSuccess) {
    CommandSuccess const in{.command_id = 42};
    auto const decoded = decode_command_success(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsCommandFail) {
    CommandFail const in{.command_id = 42, .reason = "device offline"};
    auto const decoded = decode_command_fail(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, RoundTripsEmptyString) {
    RegisterRequest const in{.agent_tag = ""};
    auto const decoded = decode_register_request(encode(in));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, in);
}

TEST(MessageTest, EncodesU64LittleEndian) {
    CommandAck const in{.command_id = 0x0102030405060708ULL};
    auto const bytes = encode(in);
    ASSERT_EQ(bytes.size(), std::size_t{8});
    EXPECT_EQ(bytes[0], std::byte{0x08});
    EXPECT_EQ(bytes[1], std::byte{0x07});
    EXPECT_EQ(bytes[2], std::byte{0x06});
    EXPECT_EQ(bytes[3], std::byte{0x05});
    EXPECT_EQ(bytes[4], std::byte{0x04});
    EXPECT_EQ(bytes[5], std::byte{0x03});
    EXPECT_EQ(bytes[6], std::byte{0x02});
    EXPECT_EQ(bytes[7], std::byte{0x01});
}

TEST(MessageTest, EncodesU16LengthLittleEndian) {
    RegisterRequest const in{.agent_tag = "ab"};
    auto const bytes = encode(in);
    ASSERT_EQ(bytes.size(), std::size_t{4});
    EXPECT_EQ(bytes[0], std::byte{0x02});
    EXPECT_EQ(bytes[1], std::byte{0x00});
    EXPECT_EQ(bytes[2], std::byte{'a'});
    EXPECT_EQ(bytes[3], std::byte{'b'});
}

TEST(MessageTest, RejectsTruncatedLengthPrefix) {
    std::array<std::byte, 1> const src{std::byte{0x01}};
    EXPECT_FALSE(decode_register_request(src).has_value());
}

TEST(MessageTest, RejectsTruncatedStringBody) {
    std::array<std::byte, 5> const src{
        std::byte{0x05}, std::byte{0x00}, std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
    };
    EXPECT_FALSE(decode_register_request(src).has_value());
}

TEST(MessageTest, RejectsTrailingBytes) {
    std::array<std::byte, 5> const src{
        std::byte{0x02}, std::byte{0x00}, std::byte{'h'}, std::byte{'i'}, std::byte{0xff},
    };
    EXPECT_FALSE(decode_register_request(src).has_value());
}

TEST(MessageTest, RejectsNonEmptyRegisterSuccessPayload) {
    std::array<std::byte, 1> const src{std::byte{0x00}};
    EXPECT_FALSE(decode_register_success(src).has_value());
}

TEST(MessageTest, RejectsTruncatedU64) {
    std::array<std::byte, 7> const src{
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07},
    };
    EXPECT_FALSE(decode_command_ack(src).has_value());
}

} // namespace ddcs::protocol::message
