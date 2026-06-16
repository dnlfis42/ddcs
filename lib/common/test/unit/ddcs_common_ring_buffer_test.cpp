#include "ddcs/common/ring_buffer.hpp"

#include <array>
#include <cstddef>
#include <span>

#include <gtest/gtest.h>

namespace ddcs::common {

namespace {

template <std::size_t... Values>
consteval bool all_valid_capacities() {
    return (valid_ring_buffer_capacity<Values> && ...);
}

template <std::size_t... Values>
consteval bool all_invalid_capacities() {
    return ((!valid_ring_buffer_capacity<Values>) && ...);
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
    return std::span<std::byte const>{bytes.data(), n};
}

} // namespace

TEST(RingBufferTest, ReportsEmptyState) {
    RingBuffer<test_capacity> rb;

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.available(), test_capacity);
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
}

TEST(RingBufferTest, ReportsStateAfterPartialWrite) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 3)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), 3u);
    EXPECT_EQ(rb.available(), test_capacity - 3);
    EXPECT_FALSE(rb.empty());
    EXPECT_FALSE(rb.full());
}

TEST(RingBufferTest, ReportsStateAfterWrappedWrite) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};

    ASSERT_TRUE(rb.write(head(pattern, 5)));
    ASSERT_TRUE(rb.read(drained));
    ASSERT_TRUE(rb.write(head(pattern, 4)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), 4u);
    EXPECT_EQ(rb.available(), 4u);
    EXPECT_FALSE(rb.empty());
    EXPECT_FALSE(rb.full());
}

TEST(RingBufferTest, ReportsFullState) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, test_capacity)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), test_capacity);
    EXPECT_EQ(rb.available(), 0u);
    EXPECT_FALSE(rb.empty());
    EXPECT_TRUE(rb.full());
}

TEST(RingBufferTest, ReportsFullStateAfterWrappedWrite) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 3> drained{};

    ASSERT_TRUE(rb.write(head(pattern, 3)));
    ASSERT_TRUE(rb.read(drained));
    ASSERT_TRUE(rb.write(head(pattern, test_capacity)));

    EXPECT_EQ(rb.capacity(), test_capacity);
    EXPECT_EQ(rb.size(), test_capacity);
    EXPECT_EQ(rb.available(), 0u);
    EXPECT_FALSE(rb.empty());
    EXPECT_TRUE(rb.full());
}

TEST(RingBufferTest, ReturnsCurrentReadableData) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 4)));

    auto readable = rb.readable();
    ASSERT_EQ(readable.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(readable[i], pattern[i]);
    }
    EXPECT_EQ(rb.size(), 4u);
}

TEST(RingBufferTest, ReturnsContiguousReadableDataAfterWrap) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};

    ASSERT_TRUE(rb.write(head(pattern, 5)));
    ASSERT_TRUE(rb.read(drained));

    std::array<std::byte, 6> payload{
        std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6},
    };
    ASSERT_TRUE(rb.write(payload));

    auto readable = rb.readable();
    ASSERT_EQ(readable.size(), 3u);
    EXPECT_EQ(readable[0], payload[0]);
    EXPECT_EQ(readable[1], payload[1]);
    EXPECT_EQ(readable[2], payload[2]);
    EXPECT_EQ(rb.size(), 6u);
}

TEST(RingBufferTest, ShrinksWritableRegionAfterWrite) {
    RingBuffer<test_capacity> rb;
    EXPECT_EQ(rb.writable().size(), test_capacity);

    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 5)));
    EXPECT_EQ(rb.writable().size(), test_capacity - 5);
}

TEST(RingBufferTest, ExposesExternallyFilledDataOnCommit) {
    RingBuffer<test_capacity> rb;
    auto region = rb.writable();
    ASSERT_GE(region.size(), 2u);

    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};
    ASSERT_TRUE(rb.commit(2));
    EXPECT_EQ(rb.size(), 2u);

    std::array<std::byte, 2> dst{};
    ASSERT_TRUE(rb.read(dst));
    EXPECT_EQ(dst[0], std::byte{0xab});
    EXPECT_EQ(dst[1], std::byte{0xcd});
}

TEST(RingBufferTest, RejectsCommitBeyondAvailableSpace) {
    RingBuffer<test_capacity> rb;

    EXPECT_FALSE(rb.commit(test_capacity + 1));
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.available(), test_capacity);
}

TEST(RingBufferTest, RejectsConsumeBeyondReadableData) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 2> two{};
    ASSERT_TRUE(rb.write(two));

    EXPECT_FALSE(rb.consume(3));
    EXPECT_EQ(rb.size(), 2u);
}

TEST(RingBufferTest, ResetsPositionsOnClear) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 3> three{};
    ASSERT_TRUE(rb.write(three));

    rb.clear();
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.available(), test_capacity);
}

TEST(RingBufferTest, RoundTripsWrittenBytes) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 4)));

    std::array<std::byte, 4> dst{};
    ASSERT_TRUE(rb.read(dst));
    EXPECT_TRUE(rb.empty());
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(dst[i], pattern[i]);
    }
}

TEST(RingBufferTest, RejectsWriteWhenSpaceIsInsufficient) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, test_capacity + 1> too_much{};

    EXPECT_FALSE(rb.write(too_much));
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.available(), test_capacity);
}

TEST(RingBufferTest, RejectsReadWhenDataIsInsufficient) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 1> one{std::byte{0xab}};
    ASSERT_TRUE(rb.write(one));

    std::array<std::byte, 2> dst{};
    EXPECT_FALSE(rb.read(dst));
    EXPECT_EQ(rb.size(), 1u);
}

TEST(RingBufferTest, LeavesReadCursorUnchangedOnPeek) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 3)));

    std::array<std::byte, 3> peeked1{};
    std::array<std::byte, 3> peeked2{};
    ASSERT_TRUE(rb.peek(peeked1));
    ASSERT_TRUE(rb.peek(peeked2));
    EXPECT_EQ(rb.size(), 3u);

    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(peeked1[i], pattern[i]);
        EXPECT_EQ(peeked2[i], pattern[i]);
    }
}

TEST(RingBufferTest, AdvancesReadCursorOnRead) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 5)));
    EXPECT_EQ(rb.size(), 5u);

    std::array<std::byte, 3> dst{};
    ASSERT_TRUE(rb.read(dst));
    EXPECT_EQ(rb.size(), 2u);
}

TEST(RingBufferTest, RoundTripsBytesAcrossWrapBoundary) {
    RingBuffer<test_capacity> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};
    ASSERT_TRUE(rb.write(head(pattern, 5)));
    ASSERT_TRUE(rb.read(drained));

    std::array<std::byte, 6> payload{
        std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6},
    };
    ASSERT_TRUE(rb.write(payload));
    EXPECT_EQ(rb.size(), 6u);

    std::array<std::byte, 6> dst{};
    ASSERT_TRUE(rb.read(dst));
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(dst[i], payload[i]);
    }
}

TEST(RingBufferTest, PreservesContentsAcrossRepeatedWraps) {
    RingBuffer<test_capacity> rb;
    std::array<std::byte, 3> chunk{
        std::byte{0xa1},
        std::byte{0xb2},
        std::byte{0xc3},
    };

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(rb.write(chunk));
        std::array<std::byte, 3> dst{};
        ASSERT_TRUE(rb.read(dst));
        for (std::size_t j = 0; j < 3; ++j) {
            EXPECT_EQ(dst[j], chunk[j]);
        }
    }
    EXPECT_TRUE(rb.empty());
}

} // namespace ddcs::common
