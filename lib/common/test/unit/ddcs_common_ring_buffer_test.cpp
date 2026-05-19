#include "ddcs/common/ring_buffer.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <utility>

#include <cstddef>

namespace ddcs::common {

static_assert(ringbuf_capacity<1>);
static_assert(ringbuf_capacity<2>);
static_assert(ringbuf_capacity<4>);
static_assert(ringbuf_capacity<1024>);
static_assert(!ringbuf_capacity<0>);
static_assert(!ringbuf_capacity<3>);
static_assert(!ringbuf_capacity<5>);
static_assert(!ringbuf_capacity<6>);

namespace {

constexpr std::size_t kN = 8;

std::array<std::byte, kN> make_pattern() {
    return {
        std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40},
        std::byte{0x50}, std::byte{0x60}, std::byte{0x70}, std::byte{0x80},
    };
}

std::span<std::byte const> head(std::array<std::byte, kN> const& a, std::size_t n) {
    return std::span<std::byte const>{a.data(), n};
}

} // namespace

TEST(RingBufferTest, ObserversInEmptyState) {
    RingBuffer<kN> rb;
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.available(), kN);
    EXPECT_EQ(rb.capacity(), kN);
    EXPECT_EQ(rb.readable_size(), 0u);
    EXPECT_EQ(rb.writable_size(), kN);
}

TEST(RingBufferTest, ObserversInPartialNoWrapState) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 3)));

    EXPECT_FALSE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.size(), 3u);
    EXPECT_EQ(rb.available(), 5u);
    EXPECT_EQ(rb.capacity(), kN);
    EXPECT_EQ(rb.readable_size(), 3u);
    EXPECT_EQ(rb.writable_size(), 5u);
}

TEST(RingBufferTest, ObserversInPartialWrappedState) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 5> drained{};

    ASSERT_TRUE(rb.write(head(pattern, 5)));
    ASSERT_TRUE(rb.read(drained));
    ASSERT_TRUE(rb.write(head(pattern, 4))); // wraps to idx 5,6,7,0

    EXPECT_FALSE(rb.empty());
    EXPECT_FALSE(rb.full());
    EXPECT_EQ(rb.size(), 4u);
    EXPECT_EQ(rb.available(), 4u);
    EXPECT_EQ(rb.capacity(), kN);
    EXPECT_EQ(rb.readable_size(), 3u); // idx 5,6,7 contiguous
    EXPECT_EQ(rb.writable_size(), 4u); // idx 1,2,3,4 contiguous
}

TEST(RingBufferTest, ObserversInFullNoWrapState) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, kN)));

    EXPECT_FALSE(rb.empty());
    EXPECT_TRUE(rb.full());
    EXPECT_EQ(rb.size(), kN);
    EXPECT_EQ(rb.available(), 0u);
    EXPECT_EQ(rb.capacity(), kN);
    EXPECT_EQ(rb.readable_size(), kN);
    EXPECT_EQ(rb.writable_size(), 0u);
}

TEST(RingBufferTest, ObserversInFullWrappedState) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    std::array<std::byte, 3> drained{};

    ASSERT_TRUE(rb.write(head(pattern, 3)));
    ASSERT_TRUE(rb.read(drained));
    ASSERT_TRUE(rb.write(head(pattern, kN))); // 8 bytes wraps from idx 3

    EXPECT_FALSE(rb.empty());
    EXPECT_TRUE(rb.full());
    EXPECT_EQ(rb.size(), kN);
    EXPECT_EQ(rb.available(), 0u);
    EXPECT_EQ(rb.capacity(), kN);
    EXPECT_EQ(rb.readable_size(), 5u); // idx 3,4,5,6,7 contiguous
    EXPECT_EQ(rb.writable_size(), 0u);
}

TEST(RingBufferTest, WriteReadRoundTrip) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 4)));

    std::array<std::byte, 4> dst{};
    ASSERT_TRUE(rb.read(dst));
    EXPECT_TRUE(rb.empty());
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(dst[i], pattern[i]);
    }
}

TEST(RingBufferTest, WriteReturnsFalseWhenInsufficientSpace) {
    RingBuffer<kN> rb;
    std::array<std::byte, kN + 1> too_much{};
    EXPECT_FALSE(rb.write(too_much));
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, ReadReturnsFalseWhenInsufficientData) {
    RingBuffer<kN> rb;
    std::array<std::byte, 1> one{std::byte{0xab}};
    ASSERT_TRUE(rb.write(one));

    std::array<std::byte, 2> dst{};
    EXPECT_FALSE(rb.read(dst));
    EXPECT_EQ(rb.size(), 1u);
}

TEST(RingBufferTest, PeekDoesNotAdvanceReadCursor) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 3)));

    std::array<std::byte, 3> peeked1{};
    std::array<std::byte, 3> peeked2{};
    ASSERT_TRUE(rb.peek(peeked1));
    ASSERT_TRUE(rb.peek(peeked2));
    EXPECT_EQ(rb.size(), 3u);

    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(peeked1[i], peeked2[i]);
        EXPECT_EQ(peeked1[i], pattern[i]);
    }
}

TEST(RingBufferTest, ReadAdvancesReadCursor) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 5)));
    EXPECT_EQ(rb.size(), 5u);

    std::array<std::byte, 3> dst{};
    ASSERT_TRUE(rb.read(dst));
    EXPECT_EQ(rb.size(), 2u);
}

TEST(RingBufferTest, WritesAndReadsAcrossWrapBoundary) {
    RingBuffer<kN> rb;
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

TEST(RingBufferTest, ZeroCopyWriteViaWritableAndMove) {
    RingBuffer<kN> rb;
    auto region = rb.writable();
    ASSERT_EQ(region.size(), kN);

    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};
    ASSERT_TRUE(rb.move_write_pos(2));
    EXPECT_EQ(rb.size(), 2u);

    std::array<std::byte, 2> dst{};
    ASSERT_TRUE(rb.read(dst));
    EXPECT_EQ(dst[0], std::byte{0xab});
    EXPECT_EQ(dst[1], std::byte{0xcd});
}

TEST(RingBufferTest, MoveWritePosReturnsFalseWhenExceedsAvailable) {
    RingBuffer<kN> rb;
    EXPECT_FALSE(rb.move_write_pos(kN + 1));
    EXPECT_TRUE(rb.empty());
}

TEST(RingBufferTest, MoveReadPosReturnsFalseWhenExceedsSize) {
    RingBuffer<kN> rb;
    std::array<std::byte, 2> two{};
    ASSERT_TRUE(rb.write(two));

    EXPECT_FALSE(rb.move_read_pos(3));
    EXPECT_EQ(rb.size(), 2u);
}

TEST(RingBufferTest, ClearResetsToEmpty) {
    RingBuffer<kN> rb;
    std::array<std::byte, 3> three{};
    ASSERT_TRUE(rb.write(three));
    rb.clear();
    EXPECT_TRUE(rb.empty());
    EXPECT_EQ(rb.size(), 0u);
    EXPECT_EQ(rb.available(), kN);
}

TEST(RingBufferTest, IsMoveConstructible) {
    RingBuffer<kN> rb;
    auto pattern = make_pattern();
    ASSERT_TRUE(rb.write(head(pattern, 3)));

    RingBuffer<kN> moved{std::move(rb)};
    EXPECT_EQ(moved.size(), 3u);

    std::array<std::byte, 3> dst{};
    ASSERT_TRUE(moved.read(dst));
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(dst[i], pattern[i]);
    }
}

TEST(RingBufferTest, PreservesContentsAcrossManyWrapCycles) {
    RingBuffer<kN> rb;
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
