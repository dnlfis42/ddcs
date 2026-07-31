#include "ddcs/wire/command/command.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

namespace {

using ddcs::wire::command::CommandType;
using ddcs::wire::command::decode_set_mode;
using ddcs::wire::command::encode_set_mode;

constexpr std::size_t buffer_capacity = 64;

TEST(CommandTest, RoundTripsSetOperatingMode) {
    std::array<std::byte, buffer_capacity> buf{};

    auto const written = encode_set_mode(buf, 2);
    ASSERT_TRUE(written.has_value());

    auto const out = decode_set_mode(std::span<std::byte const>{buf}.first(*written));
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->mode, 2);
}

TEST(CommandTest, RejectsTooSmallOutput) {
    EXPECT_FALSE(encode_set_mode(std::span<std::byte>{}, 0).has_value());
}

TEST(CommandTest, RejectsEmptyPayload) {
    EXPECT_FALSE(decode_set_mode(std::span<std::byte const>{}).has_value());
}

TEST(CommandTest, RejectsOversizedPayload) {
    std::array<std::byte, 2> two{};
    EXPECT_FALSE(decode_set_mode({two.data(), two.size()}).has_value());
}

TEST(CommandTest, CarriesRawModeByte) {
    // wire::command는 raw u8만 싣는다. 어휘 검증은 수신측(device::decode_mode)이 한다.
    std::array<std::byte, 1> const raw{std::byte{0xFF}};
    auto const out = decode_set_mode(raw);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->mode, 0xFFu);
}

TEST(CommandTest, MapsCommandTypeToWireValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(CommandType::invalid), 0x00);
    EXPECT_EQ(static_cast<std::uint8_t>(CommandType::set_mode), 0x01);
}

} // namespace
