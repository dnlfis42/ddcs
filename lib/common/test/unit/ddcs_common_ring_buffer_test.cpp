#include "ddcs/common/ring_buffer.hpp"

#include <array>
#include <cstddef>
#include <span>

#include <gtest/gtest.h>

namespace {

using ddcs::common::RingBuffer;

template <std::size_t... Values>
consteval bool all_valid_capacities() {
    return (requires { typename RingBuffer<Values>; } && ...);
}

template <std::size_t... Values>
consteval bool all_invalid_capacities() {
    return ((!requires { typename RingBuffer<Values>; }) && ...);
}

static_assert(all_valid_capacities<1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024>());
static_assert(all_invalid_capacities<0, 3, 5, 6, 7, 9, 10>());

constexpr std::size_t test_capacity = 8;

std::array<std::byte, test_capacity> make_pattern() {
    return {
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
        std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80},
    };
}

template <std::size_t N>
std::span<std::byte const> head(std::array<std::byte, N> const& bytes, std::size_t n) {
    return {bytes.data(), n};
}

template <std::size_t N>
std::span<std::byte const>
slice(std::array<std::byte, N> const& bytes, std::size_t offset, std::size_t n) {
    return {bytes.data() + offset, n};
}

void expect_bytes_eq(std::span<std::byte const> actual, std::span<std::byte const> expected) {
    ASSERT_EQ(actual.size(), expected.size());

    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i], expected[i]);
    }
}

TEST(RingBufferTest, StartsEmpty) {
    RingBuffer<test_capacity> rb;

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.writable_size(), test_capacity);
    EXPECT_TRUE(rb.readable_span().empty());
    EXPECT_EQ(rb.writable_span().size(), test_capacity);
}

TEST(RingBufferTest, ReportsStateAfterWrite) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    ASSERT_TRUE(rb.try_write(head(pattern, 3)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), 3u);
    EXPECT_FALSE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.writable_size(), test_capacity - 3u);
}

TEST(RingBufferTest, ReportsStateAfterWrappedWrite) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};

    ASSERT_TRUE(rb.try_write(head(pattern, 5)));
    ASSERT_TRUE(rb.try_read(drained));
    ASSERT_TRUE(rb.try_write(head(pattern, 4)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), 4u);
    EXPECT_FALSE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.writable_size(), 4u);
}

TEST(RingBufferTest, ReportsFullState) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    ASSERT_TRUE(rb.try_write(head(pattern, test_capacity)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), test_capacity);
    EXPECT_FALSE(rb.empty());
    EXPECT_TRUE(rb.full());
    EXPECT_EQ(rb.writable_size(), 0u);
}

TEST(RingBufferTest, ReportsFullStateAfterWrappedWrite) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 3> drained{};

    ASSERT_TRUE(rb.try_write(head(pattern, 3)));
    ASSERT_TRUE(rb.try_read(drained));
    ASSERT_TRUE(rb.try_write(head(pattern, test_capacity)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), test_capacity);
    EXPECT_FALSE(rb.empty());
    EXPECT_TRUE(rb.full());
    EXPECT_EQ(rb.writable_size(), 0u);
}

TEST(RingBufferTest, ReturnsCurrentReadableSpan) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    ASSERT_TRUE(rb.try_write(head(pattern, 4)));

    expect_bytes_eq(rb.readable_span(), head(pattern, 4));
    EXPECT_EQ(rb.size(), 4u);
}

TEST(RingBufferTest, ReturnsCurrentWritableSpan) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    EXPECT_EQ(rb.writable_span().size(), test_capacity);

    ASSERT_TRUE(rb.try_write(head(pattern, 5)));

    EXPECT_EQ(rb.writable_span().size(), test_capacity - 5u);
}

TEST(RingBufferTest, ReturnsContiguousReadableSpanAfterWrap) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};

    ASSERT_TRUE(rb.try_write(head(pattern, 5)));
    ASSERT_TRUE(rb.try_read(drained));

    std::array<std::byte, 6> payload{
        std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6},
    };

    ASSERT_TRUE(rb.try_write(payload));

    EXPECT_EQ(rb.size(), payload.size());
    expect_bytes_eq(rb.readable_span(), head(payload, 3));
}

TEST(RingBufferTest, PeeksWithoutConsumingData) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    ASSERT_TRUE(rb.try_write(head(pattern, 3)));

    std::array<std::byte, 3> peeked1{};
    std::array<std::byte, 3> peeked2{};
    ASSERT_TRUE(rb.try_peek(peeked1));
    ASSERT_TRUE(rb.try_peek(peeked2));

    EXPECT_EQ(rb.size(), 3u);
    expect_bytes_eq(std::span<std::byte const>{peeked1}, head(pattern, 3));
    expect_bytes_eq(std::span<std::byte const>{peeked2}, head(pattern, 3));
}

TEST(RingBufferTest, ReadsDataAndConsumesIt) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    ASSERT_TRUE(rb.try_write(head(pattern, 4)));

    std::array<std::byte, 3> dst{};
    ASSERT_TRUE(rb.try_read(dst));

    EXPECT_EQ(rb.size(), 1u);
    EXPECT_EQ(rb.writable_size(), test_capacity - 1u);
    expect_bytes_eq(std::span<std::byte const>{dst}, head(pattern, 3));
    expect_bytes_eq(rb.readable_span(), slice(pattern, 3, 1));
}

TEST(RingBufferTest, RejectsReadWhenDataIsInsufficient) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 1> one{std::byte{0xab}};

    ASSERT_TRUE(rb.try_write(one));

    std::array<std::byte, 2> dst{};
    EXPECT_FALSE(rb.try_read(dst));

    EXPECT_EQ(rb.size(), 1u);
    expect_bytes_eq(rb.readable_span(), std::span<std::byte const>{one});
}

TEST(RingBufferTest, ConsumesDataWithoutCopying) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    ASSERT_TRUE(rb.try_write(head(pattern, 4)));

    ASSERT_TRUE(rb.try_consume(2));

    EXPECT_EQ(rb.size(), 2u);
    EXPECT_EQ(rb.writable_size(), test_capacity - 2u);
    expect_bytes_eq(rb.readable_span(), slice(pattern, 2, 2));
}

TEST(RingBufferTest, RejectsConsumeWhenDataIsInsufficient) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 2> two{};

    ASSERT_TRUE(rb.try_write(two));

    EXPECT_FALSE(rb.try_consume(3));

    EXPECT_EQ(rb.size(), 2u);
    EXPECT_EQ(rb.writable_size(), test_capacity - 2u);
}

TEST(RingBufferTest, WritesDataToTail) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();

    ASSERT_TRUE(rb.try_write(head(pattern, 2)));
    ASSERT_TRUE(rb.try_write(slice(pattern, 2, 2)));

    EXPECT_EQ(rb.size(), 4u);
    EXPECT_EQ(rb.writable_size(), test_capacity - 4u);
    expect_bytes_eq(rb.readable_span(), head(pattern, 4));
}

TEST(RingBufferTest, RejectsWriteWhenSpaceIsInsufficient) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, test_capacity + 1> too_much{};

    EXPECT_FALSE(rb.try_write(too_much));

    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.writable_size(), test_capacity);
}

TEST(RingBufferTest, CommitsBytesWrittenToWritableSpan) {
    RingBuffer<test_capacity> rb;
    auto region = rb.writable_span();
    ASSERT_GE(region.size(), 2u);
    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};

    ASSERT_TRUE(rb.try_commit(2));

    std::array<std::byte, 2> expected{std::byte{0xab}, std::byte{0xcd}};
    EXPECT_EQ(rb.size(), expected.size());
    EXPECT_EQ(rb.writable_size(), test_capacity - expected.size());
    expect_bytes_eq(rb.readable_span(), std::span<std::byte const>{expected});
}

TEST(RingBufferTest, CommitsBytesWrittenAtWrapBoundary) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};

    ASSERT_TRUE(rb.try_write(head(pattern, 5)));
    ASSERT_TRUE(rb.try_read(drained));

    auto region = rb.writable_span();
    ASSERT_EQ(region.size(), 3u);
    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};
    region[2] = std::byte{0xef};

    ASSERT_TRUE(rb.try_commit(region.size()));

    std::array<std::byte, 3> expected{std::byte{0xab}, std::byte{0xcd}, std::byte{0xef}};
    EXPECT_EQ(rb.size(), expected.size());
    expect_bytes_eq(rb.readable_span(), std::span<std::byte const>{expected});
}

TEST(RingBufferTest, RejectsCommitWhenWritableSpaceIsInsufficient) {
    RingBuffer<test_capacity> rb;

    EXPECT_FALSE(rb.try_commit(test_capacity + 1));

    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.writable_size(), test_capacity);
}

TEST(RingBufferTest, ClearsBufferState) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 3> three{};

    ASSERT_TRUE(rb.try_write(three));
    ASSERT_TRUE(rb.try_consume(1));

    rb.clear();

    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.writable_size(), test_capacity);
}

TEST(RingBufferTest, ResetsBufferState) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 3> three{};

    ASSERT_TRUE(rb.try_write(three));

    rb.reset();

    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.writable_size(), test_capacity);
}

TEST(RingBufferTest, RoundTripsBytesAcrossWrapBoundary) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};

    ASSERT_TRUE(rb.try_write(head(pattern, 5)));
    ASSERT_TRUE(rb.try_read(drained));

    std::array<std::byte, 6> payload{
        std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6},
    };

    ASSERT_TRUE(rb.try_write(payload));

    std::array<std::byte, 6> dst{};
    ASSERT_TRUE(rb.try_read(dst));

    expect_bytes_eq(std::span<std::byte const>{dst}, std::span<std::byte const>{payload});
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, PreservesContentsAcrossRepeatedWraps) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 3> chunk{
        std::byte{0xa1},
        std::byte{0xb2},
        std::byte{0xc3},
    };

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(rb.try_write(chunk));

        std::array<std::byte, 3> dst{};
        ASSERT_TRUE(rb.try_read(dst));
        expect_bytes_eq(std::span<std::byte const>{dst}, std::span<std::byte const>{chunk});
    }

    EXPECT_TRUE(rb.empty());
}

} // namespace
