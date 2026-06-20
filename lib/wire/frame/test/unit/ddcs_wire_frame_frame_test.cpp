#include "ddcs/wire/frame/frame.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

namespace {

using ddcs::wire::frame::decode;
using ddcs::wire::frame::encode;
using ddcs::wire::frame::HeaderBytes;

TEST(FrameTest, RoundTripsTypicalHeader) {
    EXPECT_EQ(decode(encode(1024)), std::optional<std::uint16_t>{1024});
}

TEST(FrameTest, RoundTripsEmptyPayload) {
    EXPECT_EQ(decode(encode(0)), std::optional<std::uint16_t>{0});
}

TEST(FrameTest, RoundTripsMaxLength) {
    EXPECT_EQ(decode(encode(0xFFFF)), std::optional<std::uint16_t>{0xFFFF});
}

TEST(FrameTest, EncodesMagicAsBigEndian) {
    auto const bytes = encode(0);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0xDDu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[1]), 0xC5u);
}

TEST(FrameTest, EncodesLengthAsBigEndian) {
    auto const bytes = encode(0x1234);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[2]), 0x12u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x34u);
}

TEST(FrameTest, RejectsInvalidMagic) {
    HeaderBytes const bytes{std::byte{0xCA}, std::byte{0xFE}, std::byte{0x00}, std::byte{0x00}};
    EXPECT_FALSE(decode(bytes).has_value());
}

} // namespace
