#include "ddcs/common/object_pool.hpp"

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ObjectPool;
using ddcs::common::PoolHandle;
using ddcs::common::detail::object_pool::resettable;

struct Item {
    void reset() noexcept {
        value = 0;
        ++reset_count;
    }

    int value = 0;
    int reset_count = 0;
};

struct ConstructedItem {
    explicit ConstructedItem(int value_in) noexcept
        : value(value_in) {}

    void reset() noexcept {
        value = 0;
    }

    int value;
};

struct ThrowingResetItem {
    void reset() {}
};

struct NonVoidResetItem {
    int reset() noexcept {
        return 0;
    }
};

template <std::size_t ChunkSize>
concept valid_item_pool_chunk_size = requires { ObjectPool<Item>::template create<ChunkSize>(); };

static_assert(!std::is_copy_constructible_v<ObjectPool<Item>>);
static_assert(!std::is_copy_assignable_v<ObjectPool<Item>>);
static_assert(!std::is_move_constructible_v<ObjectPool<Item>>);
static_assert(!std::is_move_assignable_v<ObjectPool<Item>>);

static_assert(std::is_same_v<PoolHandle<Item>, ObjectPool<Item>::Handle>);

static_assert(resettable<Item>);
static_assert(!resettable<ThrowingResetItem>);
static_assert(!resettable<NonVoidResetItem>);

static_assert(!valid_item_pool_chunk_size<0>);
static_assert(valid_item_pool_chunk_size<1>);
static_assert(valid_item_pool_chunk_size<64>);

TEST(ObjectPoolTest, UsesDefaultChunkSize) {
    auto pool = ObjectPool<Item>::create();

    EXPECT_EQ(pool.chunk_size(), 64u);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.available_count(), 0u);
    EXPECT_EQ(pool.acquired_count(), 0u);
}

TEST(ObjectPoolTest, ConstructsObjectsWithCreateArguments) {
    auto pool = ObjectPool<ConstructedItem>::create<2>(42);

    auto handle = pool.acquire();
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(handle->value, 42);
}

TEST(ObjectPoolTest, ReserveZeroDoesNothing) {
    auto pool = ObjectPool<Item>::create<4>();

    pool.reserve(0);

    EXPECT_EQ(pool.chunk_size(), 4u);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.available_count(), 0u);
    EXPECT_EQ(pool.acquired_count(), 0u);
}

TEST(ObjectPoolTest, ReserveRoundsCapacityUpToChunkSize) {
    auto pool = ObjectPool<Item>::create<4>();

    pool.reserve(5);

    EXPECT_EQ(pool.chunk_size(), 4u);
    EXPECT_EQ(pool.capacity(), 8u);
    EXPECT_EQ(pool.available_count(), 8u);
    EXPECT_EQ(pool.acquired_count(), 0u);
}

TEST(ObjectPoolTest, ReserveDoesNothingWhenCapacityAlreadySatisfiesMinimum) {
    auto pool = ObjectPool<Item>::create<4>();

    pool.reserve(5);

    auto handle = pool.acquire();
    ASSERT_NE(handle.get(), nullptr);

    pool.reserve(4);

    EXPECT_EQ(pool.capacity(), 8u);
    EXPECT_EQ(pool.available_count(), 7u);
    EXPECT_EQ(pool.acquired_count(), 1u);
}

TEST(ObjectPoolTest, ReserveCanAddChunksWhileObjectsAreAcquired) {
    auto pool = ObjectPool<Item>::create<4>();

    pool.reserve(4);

    auto handle = pool.acquire();
    ASSERT_NE(handle.get(), nullptr);

    pool.reserve(9);

    EXPECT_EQ(pool.capacity(), 12u);
    EXPECT_EQ(pool.available_count(), 11u);
    EXPECT_EQ(pool.acquired_count(), 1u);
}

TEST(ObjectPoolTest, TracksCountsAcrossAcquireAndRelease) {
    auto pool = ObjectPool<Item>::create<64>();
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.available_count(), 0u);
    EXPECT_EQ(pool.acquired_count(), 0u);

    {
        auto handle = pool.acquire();
        ASSERT_NE(handle.get(), nullptr);
        EXPECT_EQ(pool.capacity(), 64u);
        EXPECT_EQ(pool.available_count(), 63u);
        EXPECT_EQ(pool.acquired_count(), 1u);
    }

    EXPECT_EQ(pool.capacity(), 64u);
    EXPECT_EQ(pool.available_count(), 64u);
    EXPECT_EQ(pool.acquired_count(), 0u);
}

TEST(ObjectPoolTest, AddsChunkWhenFreeListIsEmpty) {
    auto pool = ObjectPool<Item>::create<4>();
    std::array<PoolHandle<Item>, 5> handles{};

    EXPECT_EQ(pool.capacity(), 0u);

    for (std::size_t i = 0; i < 4; ++i) {
        handles[i] = pool.acquire();
    }

    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.available_count(), 0u);
    EXPECT_EQ(pool.acquired_count(), 4u);

    handles[4] = pool.acquire();

    EXPECT_EQ(pool.capacity(), 8u);
    EXPECT_EQ(pool.available_count(), 3u);
    EXPECT_EQ(pool.acquired_count(), 5u);
}

TEST(ObjectPoolTest, ReleasesSlotWhenHandleIsDestroyed) {
    auto pool = ObjectPool<Item>::create();

    Item* released{};
    {
        auto handle = pool.acquire();
        released = handle.get();
    }

    auto reacquired = pool.acquire();
    EXPECT_EQ(reacquired.get(), released);
}

TEST(ObjectPoolTest, ResetsObjectWhenReleased) {
    auto pool = ObjectPool<Item>::create<1>();

    Item* released{};
    {
        auto handle = pool.acquire();
        handle->value = 42;
        released = handle.get();
    }

    auto reacquired = pool.acquire();
    ASSERT_EQ(reacquired.get(), released);
    EXPECT_EQ(reacquired->value, 0);
    EXPECT_EQ(reacquired->reset_count, 1);
}

TEST(ObjectPoolTest, ReusesReleasedSlotsInLastInFirstOutOrder) {
    auto pool = ObjectPool<Item>::create<64>();

    auto first = pool.acquire();
    auto second = pool.acquire();

    Item* first_ptr = first.get();
    Item* second_ptr = second.get();

    second.reset();
    first.reset();

    auto third = pool.acquire();
    auto fourth = pool.acquire();

    EXPECT_EQ(third.get(), first_ptr);
    EXPECT_EQ(fourth.get(), second_ptr);
}

TEST(ObjectPoolTest, TransfersHandleOwnershipOnMoveConstruction) {
    auto pool = ObjectPool<Item>::create();

    auto first = pool.acquire();
    ASSERT_TRUE(static_cast<bool>(first));

    auto second = std::move(first);
    EXPECT_FALSE(static_cast<bool>(first));
    EXPECT_TRUE(static_cast<bool>(second));
}

} // namespace
