#include "ddcs/common/linear_buffer.hpp"

#include <array>
#include <cstddef>
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
    return {a.data(), n};
}

template <std::size_t N>
std::span<std::byte const>
slice(std::array<std::byte, N> const& a, std::size_t offset, std::size_t n) {
    return {a.data() + offset, n};
}

void expect_bytes_eq(std::span<std::byte const> actual, std::span<std::byte const> expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(actual[i], expected[i]);
    }
}

} // namespace

TEST(LinearBufferTest, StartsEmpty) {
    LinearBuffer lb{test_capacity};

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.capacity(), test_capacity);
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity);
    EXPECT_TRUE(lb.data_span().empty());
}

TEST(LinearBufferTest, ReportsDataAndRoomSizesAfterAppend) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();

    ASSERT_TRUE(lb.try_append(head(pattern, 3)));

    EXPECT_FALSE(lb.empty());
    EXPECT_EQ(lb.capacity(), test_capacity);
    EXPECT_EQ(lb.size(), 3u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity - 3u);
}

TEST(LinearBufferTest, ReturnsCurrentDataSpan) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();

    ASSERT_TRUE(lb.try_append(head(pattern, 4)));

    expect_bytes_eq(lb.data_span(), head(pattern, 4));
    EXPECT_EQ(lb.size(), 4u);
}

TEST(LinearBufferTest, ReturnsCurrentTailroomSpan) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();

    EXPECT_EQ(lb.tailroom_span().size(), test_capacity);

    ASSERT_TRUE(lb.try_append(head(pattern, 5)));

    EXPECT_EQ(lb.tailroom_span().size(), test_capacity - 5u);
}

TEST(LinearBufferTest, PeeksWithoutConsumingData) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.try_append(head(pattern, 3)));

    std::array<std::byte, 3> peeked1{};
    std::array<std::byte, 3> peeked2{};
    ASSERT_TRUE(lb.try_peek(peeked1));
    ASSERT_TRUE(lb.try_peek(peeked2));

    EXPECT_EQ(lb.size(), 3u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    expect_bytes_eq(std::span<std::byte const>{peeked1}, head(pattern, 3));
    expect_bytes_eq(std::span<std::byte const>{peeked2}, head(pattern, 3));
}

TEST(LinearBufferTest, ExtractsDataAndConsumesIt) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.try_append(head(pattern, 4)));

    std::array<std::byte, 3> dst{};
    ASSERT_TRUE(lb.try_extract(dst));

    EXPECT_EQ(lb.size(), 1u);
    EXPECT_EQ(lb.headroom_size(), 3u);
    expect_bytes_eq(std::span<std::byte const>{dst}, head(pattern, 3));
    expect_bytes_eq(lb.data_span(), slice(pattern, 3, 1));
}

TEST(LinearBufferTest, RejectsExtractWhenDataIsInsufficient) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 1> one{std::byte{0xab}};
    ASSERT_TRUE(lb.try_append(one));

    std::array<std::byte, 2> dst{};
    EXPECT_FALSE(lb.try_extract(dst));

    EXPECT_EQ(lb.size(), 1u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    expect_bytes_eq(lb.data_span(), std::span<std::byte const>{one});
}

TEST(LinearBufferTest, ConsumesDataWithoutCopying) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.try_append(head(pattern, 4)));

    ASSERT_TRUE(lb.try_consume(2));

    EXPECT_EQ(lb.size(), 2u);
    EXPECT_EQ(lb.headroom_size(), 2u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity - 4u);
    expect_bytes_eq(lb.data_span(), slice(pattern, 2, 2));
}

TEST(LinearBufferTest, RejectsConsumeWhenDataIsInsufficient) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 2> two{};
    ASSERT_TRUE(lb.try_append(two));

    EXPECT_FALSE(lb.try_consume(3));

    EXPECT_EQ(lb.size(), 2u);
    EXPECT_EQ(lb.headroom_size(), 0u);
}

TEST(LinearBufferTest, AppendsDataToTail) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();

    ASSERT_TRUE(lb.try_append(head(pattern, 2)));
    ASSERT_TRUE(lb.try_append(slice(pattern, 2, 2)));

    EXPECT_EQ(lb.size(), 4u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity - 4u);
    expect_bytes_eq(lb.data_span(), head(pattern, 4));
}

TEST(LinearBufferTest, RejectsAppendWhenTailroomIsInsufficient) {
    LinearBuffer lb{2};
    std::array<std::byte, 4> too_much{};

    EXPECT_FALSE(lb.try_append(too_much));

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), 2u);
}

TEST(LinearBufferTest, CommitsBytesWrittenToTailroom) {
    LinearBuffer lb{test_capacity};
    auto region = lb.tailroom_span();
    ASSERT_GE(region.size(), 2u);
    region[0] = std::byte{0xab};
    region[1] = std::byte{0xcd};

    ASSERT_TRUE(lb.try_commit(2));

    EXPECT_EQ(lb.size(), 2u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity - 2u);

    std::array<std::byte, 2> expected{std::byte{0xab}, std::byte{0xcd}};
    expect_bytes_eq(lb.data_span(), std::span<std::byte const>{expected});
}

TEST(LinearBufferTest, RejectsCommitWhenTailroomIsInsufficient) {
    LinearBuffer lb{4};

    EXPECT_FALSE(lb.try_commit(5));

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), 4u);
}

TEST(LinearBufferTest, PrependsDataIntoHeadroom) {
    LinearBuffer lb{test_capacity};
    ASSERT_TRUE(lb.try_grow_headroom(2));

    std::array<std::byte, 3> body{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}};
    ASSERT_TRUE(lb.try_append(body));

    std::array<std::byte, 2> header{std::byte{0x01}, std::byte{0x02}};
    ASSERT_TRUE(lb.try_prepend(header));

    std::array<std::byte, 5> expected{
        std::byte{0x01}, std::byte{0x02}, std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc},
    };
    EXPECT_EQ(lb.size(), expected.size());
    EXPECT_EQ(lb.headroom_size(), 0u);
    expect_bytes_eq(lb.data_span(), std::span<std::byte const>{expected});
}

TEST(LinearBufferTest, RejectsPrependWhenHeadroomIsInsufficient) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 3> body{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}};
    ASSERT_TRUE(lb.try_append(body));

    std::array<std::byte, 2> header{};
    EXPECT_FALSE(lb.try_prepend(header));

    EXPECT_EQ(lb.size(), body.size());
    EXPECT_EQ(lb.headroom_size(), 0u);
    expect_bytes_eq(lb.data_span(), std::span<std::byte const>{body});
}

TEST(LinearBufferTest, SetsHeadroomWhenEmpty) {
    LinearBuffer lb{test_capacity};
    ASSERT_TRUE(lb.try_grow_headroom(2));

    ASSERT_TRUE(lb.try_set_headroom(5));

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.headroom_size(), 5u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity - 5u);
}

TEST(LinearBufferTest, GrowsHeadroomWhenEmpty) {
    LinearBuffer lb{test_capacity};

    ASSERT_TRUE(lb.try_grow_headroom(2));
    ASSERT_TRUE(lb.try_grow_headroom(3));

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.headroom_size(), 5u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity - 5u);
}

TEST(LinearBufferTest, StacksHeadroomWhileEmpty) {
    LinearBuffer lb{32};
    ASSERT_TRUE(lb.try_grow_headroom(5));
    ASSERT_TRUE(lb.try_grow_headroom(9));
    EXPECT_EQ(lb.headroom_size(), 14u);
    EXPECT_EQ(lb.tailroom_size(), 32u - 14u);

    std::array<std::byte, 2> const payload{std::byte{0xaa}, std::byte{0xbb}};
    std::array<std::byte, 9> const inner{};
    std::array<std::byte, 5> const outer{};
    ASSERT_TRUE(lb.try_append(payload));
    ASSERT_TRUE(lb.try_prepend(inner));
    ASSERT_TRUE(lb.try_prepend(outer));

    EXPECT_EQ(lb.size(), 16u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_FALSE(lb.try_prepend(std::span<std::byte const>{outer.data(), 1}));
}

TEST(LinearBufferTest, RejectsHeadroomChangeWhenNotEmpty) {
    LinearBuffer lb{test_capacity};
    std::array<std::byte, 1> one{std::byte{0xff}};
    ASSERT_TRUE(lb.try_append(one));

    EXPECT_FALSE(lb.try_set_headroom(4));
    EXPECT_FALSE(lb.try_grow_headroom(4));

    EXPECT_EQ(lb.size(), 1u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity - 1u);
}

TEST(LinearBufferTest, RejectsHeadroomBeyondCapacity) {
    LinearBuffer lb{4};

    EXPECT_FALSE(lb.try_set_headroom(5));
    EXPECT_FALSE(lb.try_grow_headroom(5));

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), 4u);
}

TEST(LinearBufferTest, ClearsBufferState) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.try_grow_headroom(2));
    ASSERT_TRUE(lb.try_append(head(pattern, 3)));
    ASSERT_TRUE(lb.try_consume(1));

    lb.clear();

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity);
}

TEST(LinearBufferTest, ResetsBufferState) {
    LinearBuffer lb{test_capacity};
    auto pattern = make_pattern();
    ASSERT_TRUE(lb.try_set_headroom(4));
    ASSERT_TRUE(lb.try_append(head(pattern, 2)));

    lb.reset();

    EXPECT_TRUE(lb.empty());
    EXPECT_EQ(lb.size(), 0u);
    EXPECT_EQ(lb.headroom_size(), 0u);
    EXPECT_EQ(lb.tailroom_size(), test_capacity);
}

} // namespace ddcs::common
