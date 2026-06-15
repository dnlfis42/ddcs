#include "ddcs/wire/frame/frame.hpp"

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace ddcs::wire::frame {

TEST(WireFrameTest, RoundTripsTypicalHeader) {
    Header const in{magic_value, 1024};
    EXPECT_EQ(decode(encode(in.payload_length)), in);
}

TEST(WireFrameTest, RoundTripsEmptyPayload) {
    Header const in{magic_value, 0};
    EXPECT_EQ(decode(encode(in.payload_length)), in);
}

TEST(WireFrameTest, RoundTripsLengthLimit) {
    Header const in{magic_value, static_cast<std::uint16_t>(max_encodable_payload_size)};
    EXPECT_EQ(decode(encode(in.payload_length)), in);
}

TEST(WireFrameTest, EncodesMagicAsBigEndian) {
    Header const in{magic_value, 0};
    auto const bytes = encode(in.payload_length);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0xDDu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[1]), 0xC5u);
}

TEST(WireFrameTest, EncodesLengthAsBigEndian) {
    Header const in{magic_value, 0x1234};
    auto const bytes = encode(in.payload_length);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[2]), 0x12u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x34u);
}

TEST(WireFrameTest, DecodesInvalidMagicWithoutValidation) {
    HeaderBytes const bytes{std::byte{0xCA}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00}};
    EXPECT_NE(decode(bytes).magic, magic_value);
}

TEST(WireFrameTest, ParsesHeaderWithExpectedMagic) {
    Header const in{magic_value, 9};
    auto const parsed = parse(encode(in.payload_length));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(*parsed, in);
}

TEST(WireFrameTest, RejectsHeaderWithUnexpectedMagic) {
    HeaderBytes const bytes{std::byte{0xCA}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00}};
    EXPECT_FALSE(parse(bytes));
}

} // namespace ddcs::wire::frame
