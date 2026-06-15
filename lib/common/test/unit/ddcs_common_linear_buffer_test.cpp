#include "ddcs/common/linear_buffer.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <span>

#include <gtest/gtest.h>

namespace ddcs::common {

namespace {

constexpr std::size_t test_capacity = 16;

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

TEST(LinearBufferTest, ReportsEmptyState) {
    LinearBuffer lb{test_capacity};
    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), test_capacity);
    EXPECT_EQ(lb.capacity(), test_capacity);
}

TEST(LinearBufferTest, ReportsStateAfterPartialWrite) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 3)));

    EXPECT_FALSE(lb.empty());
    EXPECT_EQ(lb.size(), 3u);
    EXPECT_EQ(lb.available(), test_capacity - 3);
    EXPECT_EQ(lb.capacity(), test_capacity);
}

TEST(LinearBufferTest, ReturnsCurrentReadableData) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 4)));

    auto r = lb.readable();
    ASSERT_EQ(r.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(r[i], pattern[i]);
    }
    EXPECT_EQ(lb.size(), 4u);
}

TEST(LinearBufferTest, ShrinksWritableRegionAfterWrite) {
    LinearBuffer lb{test_capacity};
    EXPECT_EQ(lb.writable().size(), test_capacity);

    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 5)));
    EXPECT_EQ(lb.writable().size(), test_capacity - 5);
}

TEST(LinearBufferTest, AdvancesBothCursorsWhenReservingFront) {
    LinearBuffer lb{test_capacity};
    ASSERT_TRUE(lb.reserve_front(4));
    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), test_capacity - 4);
}

TEST(LinearBufferTest, RejectsReserveFrontWhenBufferIsNotEmpty) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 1> one{std::byte{0xff}};
    ASSERT_TRUE(lb.write(one));
    EXPECT_FALSE(lb.reserve_front(4));
}

TEST(LinearBufferTest, RejectsReserveFrontLargerThanCapacity) {
    LinearBuffer lb{4};
    EXPECT_FALSE(lb.reserve_front(5));
}

TEST(LinearBufferTest, StacksReserveFrontWhileEmpty) {
    // frame 헤더 위에 message 헤더를 적층하는 시나리오: reserve(5), reserve(9), payload 순서로 쌓고,
    // write_front 역순 소모 후 read 커서가 정확히 0으로 돌아온다.
    LinearBuffer lb{32};
    ASSERT_TRUE(lb.reserve_front(5));
    ASSERT_TRUE(lb.reserve_front(9));
    EXPECT_EQ(lb.available(), 32u - 14u);

    std::array<std::byte, 2> const payload{std::byte{0xaa}, std::byte{0xbb}};
    std::array<std::byte, 9> const inner{};
    std::array<std::byte, 5> const outer{};
    ASSERT_TRUE(lb.write(payload));
    ASSERT_TRUE(lb.write_front(inner));
    ASSERT_TRUE(lb.write_front(outer));

    EXPECT_EQ(lb.size(), 16u);                       // 5 + 9 + 2, read_pos == 0
    EXPECT_FALSE(lb.write_front({outer.data(), 1})); // headroom 소진
}

TEST(LinearBufferTest, RejectsStackedReserveFrontBeyondCapacity) {
    LinearBuffer lb{8};
    ASSERT_TRUE(lb.reserve_front(5));
    EXPECT_FALSE(lb.reserve_front(4)); // 5 + 4 > 8
}

TEST(LinearBufferTest, ExposesExternallyFilledDataOnCommit) {
    LinearBuffer lb{test_capacity};
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

TEST(LinearBufferTest, RejectsCommitBeyondAvailableSpace) {
    LinearBuffer lb{4};
    EXPECT_FALSE(lb.commit(5));
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.available(), 4u);
}

TEST(LinearBufferTest, RejectsConsumeBeyondReadableData) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 2> two{};
    ASSERT_TRUE(lb.write(two));
    EXPECT_FALSE(lb.consume(3));
    EXPECT_EQ(lb.size(), 2u);
}

TEST(LinearBufferTest, RoundTripsWrittenBytes) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.write(head(pattern, 4)));

    std::array<std::byte, 4> dst{};
    ASSERT_TRUE(lb.read(dst));
    EXPECT_TRUE(lb.empty());
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_EQ(dst[i], pattern[i]);
    }
}

TEST(LinearBufferTest, RejectsWriteWhenSpaceIsInsufficient) {
    LinearBuffer lb{2};
    std::array<std::byte, 4> too_much{};
    EXPECT_FALSE(lb.write(too_much));
    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.available(), 2u);
}

TEST(LinearBufferTest, RejectsReadWhenDataIsInsufficient) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 1> one{std::byte{0xab}};
    ASSERT_TRUE(lb.write(one));

    std::array<std::byte, 2> dst{};
    EXPECT_FALSE(lb.read(dst));
    EXPECT_EQ(lb.size(), 1u);
}

TEST(LinearBufferTest, LeavesReadCursorUnchangedOnPeek) {
    LinearBuffer lb{test_capacity};
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

TEST(LinearBufferTest, PrependsIntoReservedFrontSpaceWithWriteFront) {
    LinearBuffer lb{test_capacity};
    ASSERT_TRUE(lb.reserve_front(2));

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

TEST(LinearBufferTest, RejectsWriteFrontWithoutHeadroom) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 3> body{};
    ASSERT_TRUE(lb.write(body));

    std::array<std::byte, 2> header{};
    EXPECT_FALSE(lb.write_front(header));
    EXPECT_EQ(lb.size(), 3u);
}

} // namespace ddcs::common
