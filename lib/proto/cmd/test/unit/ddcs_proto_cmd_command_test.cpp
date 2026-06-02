#include "ddcs/device/mode.hpp"
#include "ddcs/proto/cmd/command.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

#include <cstddef>

namespace {

using namespace ddcs::proto::cmd;
using ddcs::common::LinearBuffer;
using ddcs::device::Mode;

constexpr std::size_t buf_capacity{64};

} // namespace

TEST(CommandCodecTest, RoundTripsSetMode) {
    LinearBuffer buf{buf_capacity};
    SetMode const in{.mode = Mode::performance};
    ASSERT_TRUE(encode(in, buf));
    SetMode out{};
    ASSERT_TRUE(decode(buf.readable(), out));
    EXPECT_EQ(in, out);
}

TEST(CommandCodecTest, RejectsEmptyPayload) {
    SetMode out{};
    EXPECT_FALSE(decode(std::span<std::byte const>{}, out));
}

TEST(CommandCodecTest, RejectsOversizedPayload) {
    std::array<std::byte, 2> two{};
    SetMode out{};
    EXPECT_FALSE(decode({two.data(), two.size()}, out));
}

TEST(CommandCodecTest, MapsTypeOfSetMode) { EXPECT_EQ(type_of<SetMode>, CommandType::SetMode); }
