#include "ddcs/core/linear_buffer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <utility>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ddcs::core {

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

// --- cursor advance ---

TEST(LinearBufferTest, MoveWritePosExposesExternallyFilledData) {
    LinearBuffer lb{kCap};
    auto region = lb.writable();
    ASSERT_GE(region.size(), 2u);
    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};
    ASSERT_TRUE(lb.move_write_pos(2));
    EXPECT_EQ(lb.size(), 2u);

    std::array<std::byte, 2> dst{};
    ASSERT_TRUE(lb.read(dst));
    EXPECT_EQ(dst[0], std::byte{0xab});
    EXPECT_EQ(dst[1], std::byte{0xcd});
}

TEST(LinearBufferTest, MoveWritePosReturnsFalseOnOverflow) {
    LinearBuffer lb{4};
    EXPECT_FALSE(lb.move_write_pos(5));
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), 4u);
}

TEST(LinearBufferTest, MoveReadPosReturnsFalseOnUnderflow) {
    LinearBuffer lb{kCap};
    std::array<std::byte, 2> two{};
    ASSERT_TRUE(lb.write(two));
    EXPECT_FALSE(lb.move_read_pos(3));
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

// --- prefix view (LinearBuffer-only) ---

TEST(LinearBufferTest, PeekSizeReturnsPrefixSpanWithoutAdvance) {
    LinearBuffer lb{kCap};
    std::int32_t v{0x12345678};
    lb << v;

    auto s = lb.peek(sizeof(v));
    ASSERT_EQ(s.size(), sizeof(v));
    EXPECT_EQ(lb.size(), sizeof(v));

    std::int32_t recovered{};
    std::memcpy(&recovered, s.data(), sizeof(v));
    EXPECT_EQ(recovered, v);
}

TEST(LinearBufferTest, PeekSizeReturnsEmptyOnUnderflow) {
    LinearBuffer lb{kCap};
    auto s = lb.peek(std::size_t{4});
    EXPECT_TRUE(s.empty());
}

TEST(LinearBufferTest, ReadSizeReturnsSpanAndAdvances) {
    LinearBuffer lb{kCap};
    std::int32_t a{10};
    std::int32_t b{20};
    lb << a << b;

    auto s1 = lb.read(sizeof(a));
    ASSERT_EQ(s1.size(), sizeof(a));
    EXPECT_EQ(lb.size(), sizeof(b));

    std::int32_t recovered{};
    std::memcpy(&recovered, s1.data(), sizeof(a));
    EXPECT_EQ(recovered, a);
}

TEST(LinearBufferTest, ReadSizeReturnsEmptyOnUnderflow) {
    LinearBuffer lb{kCap};
    std::int32_t v{1};
    lb << v;

    auto s = lb.read(sizeof(v) + 1);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(lb.size(), sizeof(v));
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

// --- move ---

TEST(LinearBufferTest, IsMoveConstructible) {
    LinearBuffer a{kCap};
    a << 42;

    LinearBuffer b{std::move(a)};
    EXPECT_EQ(b.capacity(), kCap);
    EXPECT_EQ(b.size(), sizeof(int));

    int out{};
    b >> out;
    EXPECT_EQ(out, 42);
}

} // namespace ddcs::core
