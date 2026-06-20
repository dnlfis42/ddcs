#include "ddcs/common/endian.hpp"

#include <bit>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::common::byteswap;
using ddcs::common::from_be;
using ddcs::common::from_le;
using ddcs::common::to_be;
using ddcs::common::to_le;

TEST(EndianTest, ByteswapPreservesSingleByteValue) {
    EXPECT_EQ(byteswap(std::uint8_t{0x12}), std::uint8_t{0x12});
}

TEST(EndianTest, ByteswapReversesMultiByteUnsignedIntegers) {
    EXPECT_EQ(byteswap(std::uint16_t{0x1234}), std::uint16_t{0x3412});
    EXPECT_EQ(byteswap(std::uint32_t{0x12345678}), std::uint32_t{0x78563412});
    EXPECT_EQ(byteswap(std::uint64_t{0x0123456789ABCDEFull}), std::uint64_t{0xEFCDAB8967452301ull});
}

TEST(EndianTest, ConvertsToBigEndianRepresentation) {
    std::uint32_t const value{0x12345678};

    if constexpr (std::endian::native == std::endian::big) {
        EXPECT_EQ(to_be(value), value);
    } else {
        EXPECT_EQ(to_be(value), byteswap(value));
    }
}

TEST(EndianTest, ConvertsFromBigEndianRepresentation) {
    std::uint32_t const value{0x12345678};

    EXPECT_EQ(from_be(to_be(value)), value);
}

TEST(EndianTest, ConvertsToLittleEndianRepresentation) {
    std::uint32_t const value{0x12345678};

    if constexpr (std::endian::native == std::endian::little) {
        EXPECT_EQ(to_le(value), value);
    } else {
        EXPECT_EQ(to_le(value), byteswap(value));
    }
}

TEST(EndianTest, ConvertsFromLittleEndianRepresentation) {
    std::uint32_t const value{0x12345678};

    EXPECT_EQ(from_le(to_le(value)), value);
}

} // namespace
