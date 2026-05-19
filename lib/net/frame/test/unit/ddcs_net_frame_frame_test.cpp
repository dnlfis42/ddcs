#include "ddcs/net/frame/frame.hpp"

#include <gtest/gtest.h>

namespace ddcs::net::frame {

TEST(FrameTest, RoundTripsTypicalHeader) {
    Header const in{
        .magic = magic,
        .version = 0x01,
        .opcode = 0x42,
        .length = 1024,
    };
    EXPECT_EQ(decode(encode(in)), in);
}

TEST(FrameTest, RoundTripsEmptyPayload) {
    Header const in{
        .magic = magic,
        .version = 0x01,
        .opcode = 0x00,
        .length = 0,
    };
    EXPECT_EQ(decode(encode(in)), in);
}

TEST(FrameTest, RoundTripsMaxLength) {
    Header const in{
        .magic = magic,
        .version = 0x01,
        .opcode = 0xff,
        .length = static_cast<std::uint16_t>(max_payload),
    };
    EXPECT_EQ(decode(encode(in)), in);
}

TEST(FrameTest, DecodesInvalidMagicWithoutValidation) {
    HeaderBytes const src{
        std::byte{0xCA}, std::byte{0xFE}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    auto const decoded = decode(src);
    EXPECT_NE(decoded.magic, magic);
}

TEST(FrameTest, EncodesMagicAsBigEndian) {
    Header const in{.magic = magic, .version = 0, .opcode = 0, .length = 0};
    auto const bytes = encode(in);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0xDDu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[1]), 0xC5u);
}

TEST(FrameTest, EncodesLengthAsBigEndian) {
    Header const in{.magic = magic, .version = 0, .opcode = 0, .length = 0x1234};
    auto const bytes = encode(in);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[4]), 0x12u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[5]), 0x34u);
}

} // namespace ddcs::net::frame
