#include "ddcs/dacp/frame/frame.hpp"

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace ddcs::dacp::frame {

TEST(FrameTest, RoundTripsTypicalHeader) {
    Header const in{.magic = magic, .type = 0x20, .length = 1024};
    EXPECT_EQ(decode(encode(in)), in);
}

TEST(FrameTest, RoundTripsEmptyPayload) {
    Header const in{.magic = magic, .type = 0x10, .length = 0};
    EXPECT_EQ(decode(encode(in)), in);
}

TEST(FrameTest, RoundTripsMaxLength) {
    Header const in{.magic = magic, .type = 0x01, .length = static_cast<std::uint16_t>(length_limit)};
    EXPECT_EQ(decode(encode(in)), in);
}

TEST(FrameTest, PreservesOpaqueTypeByte) {
    Header const in{.magic = magic, .type = 0xAB, .length = 7};
    auto const bytes = encode(in);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[2]), 0xABu);
    EXPECT_EQ(decode(bytes).type, std::uint8_t{0xAB});
}

TEST(FrameTest, EncodesMagicAsBigEndian) {
    Header const in{.magic = magic, .type = 0, .length = 0};
    auto const bytes = encode(in);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0xDDu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[1]), 0xC5u);
}

TEST(FrameTest, EncodesLengthAsBigEndian) {
    Header const in{.magic = magic, .type = 0, .length = 0x1234};
    auto const bytes = encode(in);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x12u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[4]), 0x34u);
}

TEST(FrameTest, DecodesInvalidMagicWithoutValidation) {
    HeaderBytes const bytes{
        std::byte{0xCA}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    EXPECT_NE(decode(bytes).magic, magic);
}

TEST(FrameTest, ParsesHeaderWithExpectedMagic) {
    Header const in{.magic = magic, .type = 0x22, .length = 9};
    auto const parsed = parse(encode(in));
    ASSERT_TRUE(parsed);
    EXPECT_EQ(*parsed, in);
}

TEST(FrameTest, RejectsHeaderWithUnexpectedMagic) {
    HeaderBytes const bytes{
        std::byte{0xCA}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    EXPECT_FALSE(parse(bytes));
}

} // namespace ddcs::dacp::frame
