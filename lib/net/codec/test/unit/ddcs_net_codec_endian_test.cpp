#include "ddcs/net/codec/endian.hpp"

#include <gtest/gtest.h>

#include <bit>

#include <cstdint>

namespace {

using ddcs::net::codec::byteswap;
using ddcs::net::codec::from_be;
using ddcs::net::codec::from_le;
using ddcs::net::codec::to_be;
using ddcs::net::codec::to_le;

static_assert(byteswap<std::uint8_t>(0x12) == 0x12);
static_assert(byteswap<std::uint16_t>(0x1234) == 0x3412);
static_assert(byteswap<std::uint32_t>(0x12345678) == 0x78563412);
static_assert(byteswap<std::uint64_t>(0x0123456789ABCDEFull) == 0xEFCDAB8967452301ull);

TEST(EndianTest, ByteswapKnownValues) {
    EXPECT_EQ(byteswap<std::uint8_t>(0x12), 0x12);
    EXPECT_EQ(byteswap<std::uint16_t>(0x1234), 0x3412);
    EXPECT_EQ(byteswap<std::uint32_t>(0x12345678u), 0x78563412u);
    EXPECT_EQ(byteswap<std::uint64_t>(0x0123456789ABCDEFull), 0xEFCDAB8967452301ull);
}

TEST(EndianTest, RoundtripBe) {
    EXPECT_EQ(from_be(to_be<std::uint16_t>(0xBEEF)), 0xBEEF);
    EXPECT_EQ(from_be(to_be<std::uint32_t>(0xDEADBEEFu)), 0xDEADBEEFu);
    EXPECT_EQ(from_be(to_be<std::uint64_t>(0xCAFEBABEDEADBEEFull)), 0xCAFEBABEDEADBEEFull);
}

TEST(EndianTest, RoundtripLe) {
    EXPECT_EQ(from_le(to_le<std::uint16_t>(0xBEEF)), 0xBEEF);
    EXPECT_EQ(from_le(to_le<std::uint32_t>(0xDEADBEEFu)), 0xDEADBEEFu);
    EXPECT_EQ(from_le(to_le<std::uint64_t>(0xCAFEBABEDEADBEEFull)), 0xCAFEBABEDEADBEEFull);
}

TEST(EndianTest, NativeIdentity) {
    if constexpr (std::endian::native == std::endian::little) {
        EXPECT_EQ(to_le<std::uint16_t>(0x1234), 0x1234);
        EXPECT_EQ(to_be<std::uint16_t>(0x1234), 0x3412);
        EXPECT_EQ(to_le<std::uint32_t>(0x12345678u), 0x12345678u);
        EXPECT_EQ(to_be<std::uint32_t>(0x12345678u), 0x78563412u);
    } else {
        EXPECT_EQ(to_be<std::uint16_t>(0x1234), 0x1234);
        EXPECT_EQ(to_le<std::uint16_t>(0x1234), 0x3412);
        EXPECT_EQ(to_be<std::uint32_t>(0x12345678u), 0x12345678u);
        EXPECT_EQ(to_le<std::uint32_t>(0x12345678u), 0x78563412u);
    }
}

} // namespace
