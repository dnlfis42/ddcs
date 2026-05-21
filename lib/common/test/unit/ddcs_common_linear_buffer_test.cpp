#include "ddcs/common/linear_buffer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ddcs::common {

namespace {

constexpr std::size_t kCap = 16;

std::array<std::byte, 8> make_pattern() {
    return {
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
        std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80},
    };
}

template <std::size_t N>
std::span<std::byte const> head(std::array<std::byte, N> const& a, std::size_t n) {
    return std::span<std::byte const>{a.data(), n};
}

} // namespace

// --- observers ---

TEST(LinearBufferTest, ObserversInEmptyState) {
    LinearBuffer lb{kCap};
    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), kCap);
    EXPECT_EQ(lb.capacity(), kCap);
}

TEST(LinearBufferTest, ObserversAfterPartialWrite) {
    LinearBuffer lb{kCap};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 3)));

    EXPECT_FALSE(lb.empty());
    EXPECT_EQ(lb.size(), 3u);
    EXPECT_EQ(lb.available(), kCap - 3);
    EXPECT_EQ(lb.capacity(), kCap);
}

// --- stream state ---

TEST(LinearBufferTest, BoolConversionIsTrueOnInit) {
    LinearBuffer lb{kCap};
    EXPECT_TRUE(static_cast<bool>(lb));
}

TEST(LinearBufferTest, BecomesFalsyAfterStreamOverflow) {
    LinearBuffer lb{3};
    int v{1};
    lb << v;
    EXPECT_FALSE(static_cast<bool>(lb));
}

TEST(LinearBufferTest, BecomesFalsyAfterStreamUnderflow) {
    LinearBuffer lb{kCap};
    int out{};
    lb >> out;
    EXPECT_FALSE(static_cast<bool>(lb));
}

TEST(LinearBufferTest, SetFailMakesBufferFalsy) {
    LinearBuffer lb{kCap};
    lb.set_fail();
    EXPECT_FALSE(static_cast<bool>(lb));
}

TEST(LinearBufferTest, StreamOpsAreNoOpAfterFail) {
    LinearBuffer lb{kCap};
    lb.set_fail();
    int v{42};
    lb << v;
    EXPECT_EQ(lb.size(), 0u);

    int out{99};
    lb >> out;
    EXPECT_EQ(out, 99);
}

// --- zero-copy region ---

TEST(LinearBufferTest, ReadableReturnsCurrentData) {
    LinearBuffer lb{kCap};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 4)));

    auto r = lb.readable();
    ASSERT_EQ(r.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(r[i], pattern[i]);
    }
    EXPECT_EQ(lb.size(), 4u);
}

TEST(LinearBufferTest, WritableShrinksAfterWrite) {
    LinearBuffer lb{kCap};
    EXPECT_EQ(lb.writable().size(), kCap);

    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 5)));
    EXPECT_EQ(lb.writable().size(), kCap - 5);
}

// --- cursor ---

TEST(LinearBufferTest, ReserveAdvancesBothCursorsWhenEmpty) {
    LinearBuffer lb{kCap};
    ASSERT_TRUE(lb.reserve(4));
    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), kCap - 4);
}

TEST(LinearBufferTest, ReserveFailsWhenNotEmpty) {
    LinearBuffer lb{kCap};
    std::array<std::byte, 1> one{std::byte{0xff}};
    ASSERT_TRUE(lb.write(one));
    EXPECT_FALSE(lb.reserve(4));
}

TEST(LinearBufferTest, ReserveFailsWhenLargerThanCapacity) {
    LinearBuffer lb{4};
    EXPECT_FALSE(lb.reserve(5));
}

TEST(LinearBufferTest, CommitExposesExternallyFilledData) {
    LinearBuffer lb{kCap};
    auto region = lb.writable();
    ASSERT_GE(region.size(), 2u);
    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};
    ASSERT_TRUE(lb.commit(2));
    EXPECT_EQ(lb.size(), 2u);

    std::array<std::byte, 2> dst{};
    ASSERT_TRUE(lb.read(dst));
    EXPECT_EQ(dst[0], std::byte{0xab});
    EXPECT_EQ(dst[1], std::byte{0xcd});
}

TEST(LinearBufferTest, CommitReturnsFalseOnOverflow) {
    LinearBuffer lb{4};
    EXPECT_FALSE(lb.commit(5));
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), 4u);
}

TEST(LinearBufferTest, ConsumeReturnsFalseOnUnderflow) {
    LinearBuffer lb{kCap};
    std::array<std::byte, 2> two{};
    ASSERT_TRUE(lb.write(two));
    EXPECT_FALSE(lb.consume(3));
    EXPECT_EQ(lb.size(), 2u);
}

TEST(LinearBufferTest, ClearResetsPositionsAndFailFlag) {
    LinearBuffer lb{kCap};
    lb << 1 << 2 << 3;
    lb.set_fail();
    ASSERT_FALSE(static_cast<bool>(lb));

    lb.clear();
    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), kCap);
    EXPECT_TRUE(static_cast<bool>(lb));
}

// --- copy I/O (span) ---

TEST(LinearBufferTest, WriteReadRoundTrip) {
    LinearBuffer lb{kCap};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 4)));

    std::array<std::byte, 4> dst{};
    ASSERT_TRUE(lb.read(dst));
    EXPECT_TRUE(lb.empty());
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(dst[i], pattern[i]);
    }
}

TEST(LinearBufferTest, WriteReturnsFalseWhenInsufficientSpace) {
    LinearBuffer lb{2};
    std::array<std::byte, 4> too_much{};
    EXPECT_FALSE(lb.write(too_much));
    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.available(), 2u);
}

TEST(LinearBufferTest, ReadReturnsFalseWhenInsufficientData) {
    LinearBuffer lb{kCap};
    std::array<std::byte, 1> one{std::byte{0xab}};
    ASSERT_TRUE(lb.write(one));

    std::array<std::byte, 2> dst{};
    EXPECT_FALSE(lb.read(dst));
    EXPECT_EQ(lb.size(), 1u);
}

TEST(LinearBufferTest, PeekDoesNotAdvanceReadCursor) {
    LinearBuffer lb{kCap};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 3)));

    std::array<std::byte, 3> peeked1{};
    std::array<std::byte, 3> peeked2{};
    ASSERT_TRUE(lb.peek(peeked1));
    ASSERT_TRUE(lb.peek(peeked2));
    EXPECT_EQ(lb.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(peeked1[i], pattern[i]);
        EXPECT_EQ(peeked2[i], pattern[i]);
    }
}

// --- write_front (prepend into headroom) ---

TEST(LinearBufferTest, WriteFrontPrependsIntoReservedHeadroom) {
    LinearBuffer lb{kCap};
    ASSERT_TRUE(lb.reserve(2));

    std::array<std::byte, 3> body{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}};
    ASSERT_TRUE(lb.write(body));
    EXPECT_EQ(lb.size(), 3u);

    std::array<std::byte, 2> header{std::byte{0x01}, std::byte{0x02}};
    ASSERT_TRUE(lb.write_front(header));
    EXPECT_EQ(lb.size(), 5u);

    std::array<std::byte, 5> dst{};
    ASSERT_TRUE(lb.read(dst));
    EXPECT_EQ(dst[0], std::byte{0x01});
    EXPECT_EQ(dst[1], std::byte{0x02});
    EXPECT_EQ(dst[2], std::byte{0xaa});
    EXPECT_EQ(dst[3], std::byte{0xbb});
    EXPECT_EQ(dst[4], std::byte{0xcc});
}

TEST(LinearBufferTest, WriteFrontFailsWithoutHeadroom) {
    LinearBuffer lb{kCap};
    std::array<std::byte, 3> body{};
    ASSERT_TRUE(lb.write(body));

    std::array<std::byte, 2> header{};
    EXPECT_FALSE(lb.write_front(header));
    EXPECT_EQ(lb.size(), 3u);
}

// --- stream serialization ---

TEST(LinearBufferTest, StreamRoundTripsSingleInt) {
    LinearBuffer lb{kCap};
    lb << 42;
    EXPECT_EQ(lb.size(), sizeof(int));

    int out{};
    lb >> out;
    EXPECT_EQ(out, 42);
    EXPECT_TRUE(lb.empty());
    EXPECT_TRUE(static_cast<bool>(lb));
}

TEST(LinearBufferTest, StreamRoundTripsMixedArithmeticTypes) {
    LinearBuffer lb{64};
    bool b{true};
    int i{-7};
    std::uint64_t u{0xDEADBEEFCAFEBABEull};
    double d{3.141592};
    lb << b << i << u << d;
    EXPECT_EQ(lb.size(), sizeof(b) + sizeof(i) + sizeof(u) + sizeof(d));

    bool b_out{};
    int i_out{};
    std::uint64_t u_out{};
    double d_out{};
    lb >> b_out >> i_out >> u_out >> d_out;

    EXPECT_EQ(b_out, true);
    EXPECT_EQ(i_out, -7);
    EXPECT_EQ(u_out, 0xDEADBEEFCAFEBABEull);
    EXPECT_DOUBLE_EQ(d_out, 3.141592);
    EXPECT_TRUE(lb.empty());
    EXPECT_TRUE(static_cast<bool>(lb));
}

} // namespace ddcs::common
