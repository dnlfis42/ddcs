#include "ddcs/common/circular_buffer.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

using ddcs::common::CircularBuffer;

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

void expect_bytes_equal(std::span<std::byte const> actual, std::span<std::byte const> expected) {
    ASSERT_EQ(actual.size(), expected.size());

    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i], expected[i]);
    }
}

// 생성

TEST(CircularBufferTest, AcceptsValidCapacity) {
    ASSERT_NO_THROW(CircularBuffer{1});
    ASSERT_NO_THROW(CircularBuffer{2});
    ASSERT_NO_THROW(CircularBuffer{4});
    ASSERT_NO_THROW(CircularBuffer{8});
    ASSERT_NO_THROW(CircularBuffer{16});
    ASSERT_NO_THROW(CircularBuffer{32});
    ASSERT_NO_THROW(CircularBuffer{64});
    ASSERT_NO_THROW(CircularBuffer{128});
    ASSERT_NO_THROW(CircularBuffer{256});
    ASSERT_NO_THROW(CircularBuffer{512});
    ASSERT_NO_THROW(CircularBuffer{1024});
    ASSERT_NO_THROW(CircularBuffer{2048});
    ASSERT_NO_THROW(CircularBuffer{4096});
}

TEST(CircularBufferTest, RejectsInvalidCapacity) {
    ASSERT_THROW(CircularBuffer{0}, std::logic_error);
    ASSERT_THROW(CircularBuffer{3}, std::logic_error);
    ASSERT_THROW(CircularBuffer{6}, std::logic_error);
}

// 초기 상태

TEST(CircularBufferTest, StartsEmpty) {
    CircularBuffer cb{test_capacity};

    EXPECT_EQ(cb.capacity(), test_capacity);
    EXPECT_EQ(cb.size(), 0u);
    EXPECT_TRUE(cb.empty());
    EXPECT_FALSE(cb.full());
    EXPECT_TRUE(cb.readable_span().empty());
    EXPECT_EQ(cb.writable_span().size(), test_capacity);
}

// 기본 왕복

TEST(CircularBufferTest, RoundTripsBytes) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    ASSERT_TRUE(cb.write(head(pattern, 4)));
    EXPECT_EQ(cb.size(), 4u);

    std::array<std::byte, 4> out{};

    ASSERT_TRUE(cb.read(out));
    EXPECT_TRUE(cb.empty());

    expect_bytes_equal(std::span<std::byte const>{out}, head(pattern, 4));
}

// 연산 계약

TEST(CircularBufferTest, ConsumesWithoutCopying) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    ASSERT_TRUE(cb.write(head(pattern, 4)));
    EXPECT_EQ(cb.size(), 4u);

    ASSERT_TRUE(cb.consume(2));
    EXPECT_EQ(cb.size(), 2u);

    expect_bytes_equal(cb.readable_span(), slice(pattern, 2, 2));
}

TEST(CircularBufferTest, CommitsBytesWrittenToWritableSpan) {
    CircularBuffer cb{test_capacity};

    auto region = cb.writable_span();

    ASSERT_GE(region.size(), 2u);
    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};

    ASSERT_TRUE(cb.commit(2));
    EXPECT_EQ(cb.size(), 2u);

    std::array<std::byte, 2> expected{std::byte{0xab}, std::byte{0xcd}};

    expect_bytes_equal(cb.readable_span(), std::span<std::byte const>{expected});
}

TEST(CircularBufferTest, PeeksWithoutConsuming) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    ASSERT_TRUE(cb.write(head(pattern, 4)));
    EXPECT_EQ(cb.size(), 4u);

    std::array<std::byte, 4> out{};

    ASSERT_TRUE(cb.peek(out));
    EXPECT_EQ(cb.size(), 4u);

    expect_bytes_equal(std::span<std::byte const>{out}, head(pattern, 4));
}

TEST(CircularBufferTest, ReadsOldestBytesAndKeepsTheRest) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    ASSERT_TRUE(cb.write(head(pattern, 5)));
    EXPECT_EQ(cb.size(), 5u);

    std::array<std::byte, 4> out{};

    ASSERT_TRUE(cb.read(out));
    EXPECT_EQ(cb.size(), 1u);

    expect_bytes_equal(std::span<std::byte const>{out}, head(pattern, 4));

    expect_bytes_equal(cb.readable_span(), slice(pattern, 4, 1));
}

TEST(CircularBufferTest, WritesAppendToTail) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    ASSERT_TRUE(cb.write(head(pattern, 2)));
    EXPECT_EQ(cb.size(), 2u);

    ASSERT_TRUE(cb.write(slice(pattern, 2, 2)));
    EXPECT_EQ(cb.size(), 4u);

    expect_bytes_equal(cb.readable_span(), head(pattern, 4));
}

TEST(CircularBufferTest, ClearsToEmpty) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    ASSERT_TRUE(cb.write(head(pattern, 5)));

    ASSERT_TRUE(cb.consume(2));

    cb.clear();

    EXPECT_EQ(cb.size(), 0u);
    EXPECT_TRUE(cb.empty());
    EXPECT_FALSE(cb.full());
    EXPECT_TRUE(cb.readable_span().empty());
}

// 경계/거부

TEST(CircularBufferTest, RejectsConsumeWhenDataIsInsufficient) {
    CircularBuffer cb{test_capacity};

    std::array<std::byte, 2> two{};

    ASSERT_TRUE(cb.write(two));
    EXPECT_EQ(cb.size(), 2u);

    ASSERT_FALSE(cb.consume(3));
    EXPECT_EQ(cb.size(), 2u);
}

TEST(CircularBufferTest, RejectsCommitWhenWritableSpaceIsInsufficient) {
    CircularBuffer cb{test_capacity};

    ASSERT_FALSE(cb.commit(test_capacity + 1));
    EXPECT_TRUE(cb.empty());
}

TEST(CircularBufferTest, RejectsReadWhenDataIsInsufficient) {
    CircularBuffer cb{test_capacity};

    std::array<std::byte, 1> one{std::byte{0xab}};

    ASSERT_TRUE(cb.write(one));
    EXPECT_EQ(cb.size(), 1u);

    std::array<std::byte, 2> out{};

    ASSERT_FALSE(cb.read(out));
    EXPECT_EQ(cb.size(), 1u);

    expect_bytes_equal(cb.readable_span(), std::span<std::byte const>{one});
}

TEST(CircularBufferTest, RejectsWriteWhenSpaceIsInsufficient) {
    CircularBuffer cb{test_capacity};

    std::array<std::byte, test_capacity + 1> too_much{};

    ASSERT_FALSE(cb.write(too_much));
    EXPECT_TRUE(cb.empty());
}

// 상태 회계

TEST(CircularBufferTest, ReportsFullState) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    ASSERT_TRUE(cb.write(pattern));
    EXPECT_EQ(cb.capacity(), test_capacity);
    EXPECT_EQ(cb.size(), test_capacity);
    EXPECT_FALSE(cb.empty());
    EXPECT_TRUE(cb.full());
}

// wrap

void rotate(CircularBuffer& cb, std::span<std::byte const> filler) {
    ASSERT_TRUE(cb.empty());
    ASSERT_TRUE(cb.write(filler));
    ASSERT_TRUE(cb.consume(filler.size()));
    ASSERT_TRUE(cb.empty());
}

TEST(CircularBufferTest, RoundTripsBytesAcrossWrapBoundary) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    rotate(cb, head(pattern, 5));

    std::array<std::byte, 6> in{
        std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6},
    };

    ASSERT_TRUE(cb.write(in));
    EXPECT_EQ(cb.size(), 6u);

    std::array<std::byte, 6> out{};

    ASSERT_TRUE(cb.read(out));
    EXPECT_TRUE(cb.empty());

    expect_bytes_equal(std::span<std::byte const>{out}, std::span<std::byte const>{in});
}

TEST(CircularBufferTest, ReturnsContiguousReadableSpanAfterWrap) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    rotate(cb, head(pattern, 5));

    std::array<std::byte, 6> in{
        std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
        std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6},
    };

    ASSERT_TRUE(cb.write(in));
    EXPECT_EQ(cb.size(), 6u);

    expect_bytes_equal(cb.readable_span(), head(in, 3));
}

TEST(CircularBufferTest, ClampsWritableSpanAtWrapBoundary) {
    CircularBuffer cb{test_capacity};

    auto pattern = make_pattern();

    rotate(cb, head(pattern, 5));

    auto region = cb.writable_span();

    ASSERT_EQ(region.size(), 3u);

    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};
    region[2] = std::byte{0xef};

    ASSERT_TRUE(cb.commit(3));

    std::array<std::byte, 3> expected{std::byte{0xab}, std::byte{0xcd}, std::byte{0xef}};

    expect_bytes_equal(cb.readable_span(), std::span<std::byte const>{expected});
}

// 왕복 스트레스

std::byte stream_byte(std::size_t i) {
    return static_cast<std::byte>(i & 0xFF);
}

TEST(CircularBufferTest, PreservesByteStreamAcrossArbitraryChunking) {
    CircularBuffer cb{test_capacity};

    std::size_t pos = 0;

    for (std::size_t rep = 0; rep < 500; ++rep) {
        std::size_t const chunk = rep % test_capacity + 1; // 1~8

        std::array<std::byte, test_capacity> in{};

        for (std::size_t i = 0; i < chunk; ++i) {
            in[i] = stream_byte(pos + i);
        }

        ASSERT_TRUE(cb.write({in.data(), chunk}));

        std::array<std::byte, test_capacity> out{};

        ASSERT_TRUE(cb.read({out.data(), chunk}));

        for (std::size_t i = 0; i < chunk; ++i) {
            ASSERT_EQ(out[i], stream_byte(pos + i));
        }

        pos += chunk;
    }

    EXPECT_TRUE(cb.empty());
}

} // namespace
