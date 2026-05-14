#include "ddcs/protocol/frame.hpp"

#include <gtest/gtest.h>

namespace ddcs::protocol {

TEST(FrameTest, RoundTripsTypicalHeader) {
    Header const in{
        .magic = magic,
        .version = 0x01,
        .opcode = 0x42,
        .length = 1024,
    };
    EXPECT_EQ(decode_header(encode_header(in)), in);
}

TEST(FrameTest, RoundTripsEmptyPayload) {
    Header const in{
        .magic = magic,
        .version = 0x01,
        .opcode = 0x00,
        .length = 0,
    };
    EXPECT_EQ(decode_header(encode_header(in)), in);
}

TEST(FrameTest, RoundTripsMaxLength) {
    Header const in{
        .magic = magic,
        .version = 0x01,
        .opcode = 0xff,
        .length = static_cast<std::uint16_t>(max_payload),
    };
    EXPECT_EQ(decode_header(encode_header(in)), in);
}

TEST(FrameTest, DecodesInvalidMagicWithoutValidation) {
    HeaderBytes const src{
        std::byte{0xCA}, std::byte{0xFE}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    };
    auto const decoded = decode_header(src);
    EXPECT_NE(decoded.magic, magic);
}

} // namespace ddcs::protocol
