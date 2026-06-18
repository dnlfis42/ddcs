#include "ddcs/device/command.hpp"

#include "ddcs/device/mode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

namespace ddcs::device {

namespace {

constexpr std::size_t buffer_capacity{64};

} // namespace

TEST(CommandCodecTest, RoundTripsSetMode) {
    std::array<std::byte, buffer_capacity> buf{};

    auto const written = encode_set_mode(buf, Mode::performance);
    ASSERT_TRUE(written.has_value());

    auto const out = decode_set_mode(std::span<std::byte const>{buf}.first(*written));
    ASSERT_TRUE(out.has_value());

    EXPECT_EQ(out->mode, Mode::performance);
}

TEST(CommandCodecTest, RejectsTooSmallOutput) {
    EXPECT_FALSE(encode_set_mode(std::span<std::byte>{}, Mode::safe).has_value());
}

TEST(CommandCodecTest, RejectsEmptyPayload) {
    EXPECT_FALSE(decode_set_mode(std::span<std::byte const>{}).has_value());
}

TEST(CommandCodecTest, RejectsOversizedPayload) {
    std::array<std::byte, 2> two{};

    EXPECT_FALSE(decode_set_mode({two.data(), two.size()}).has_value());
}

TEST(CommandCodecTest, RejectsOutOfVocabularyMode) {
    for (std::uint8_t const raw : {std::uint8_t{0x03}, std::uint8_t{0xFF}}) {
        std::array<std::byte, 1> const one{std::byte{raw}};
        EXPECT_FALSE(decode_set_mode(one).has_value()) << "raw=" << static_cast<int>(raw);
    }
}

TEST(CommandCodecTest, MapsCommandTypeToWireValues) {
    EXPECT_EQ(static_cast<std::uint8_t>(CommandType::invalid), 0x00);
    EXPECT_EQ(static_cast<std::uint8_t>(CommandType::set_mode), 0x01);
}

} // namespace ddcs::device
