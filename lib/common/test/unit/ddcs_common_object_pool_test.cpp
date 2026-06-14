#include "ddcs/common/object_pool.hpp"

#include <array>
#include <cstddef>
#include <utility>

#include <gtest/gtest.h>

namespace ddcs::common {

namespace {

struct Item {
    int value{0};
    int reset_count{0};

    void reset() noexcept {
        value = 0;
        ++reset_count;
    }
};

struct ConstructedItem {
    explicit ConstructedItem(int value_in) noexcept : value{value_in} {}

    int value;

    void reset() noexcept { value = 0; }
};

} // namespace

TEST(ObjectPoolTest, TracksUseCountAcrossAcquireAndRelease) {
    auto pool = make_object_pool<Item>(0, 64);
    EXPECT_EQ(pool.capacity(), 0u);
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.in_use(), 0u);

    {
        auto handle = pool.acquire();
        ASSERT_NE(handle.get(), nullptr);
        EXPECT_EQ(pool.capacity(), 64u);
        EXPECT_EQ(pool.available(), 63u);
        EXPECT_EQ(pool.in_use(), 1u);
    }

    EXPECT_EQ(pool.capacity(), 64u);
    EXPECT_EQ(pool.available(), 64u);
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPoolTest, ResetsObjectWhenHandleIsReleased) {
    auto pool = make_object_pool<Item>(0, 1);

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

TEST(ObjectPoolTest, ReusesReleasedSlotsInLifoOrder) {
    auto pool = make_object_pool<Item>(0, 64);

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

TEST(ObjectPoolTest, GrowsWhenFreeListIsEmpty) {
    auto pool = make_object_pool<Item>(0, 4);
    std::array<PoolHandle<Item>, 5> handles{};

    EXPECT_EQ(pool.capacity(), 0u);

    for (std::size_t i = 0; i < 4; ++i) {
        handles[i] = pool.acquire();
    }
    EXPECT_EQ(pool.capacity(), 4u);
    EXPECT_EQ(pool.available(), 0u);
    EXPECT_EQ(pool.in_use(), 4u);

    handles[4] = pool.acquire();
    EXPECT_EQ(pool.capacity(), 8u);
    EXPECT_EQ(pool.available(), 3u);
    EXPECT_EQ(pool.in_use(), 5u);
}

TEST(ObjectPoolTest, RoundsInitialCapacityToChunkSize) {
    auto pool = make_object_pool<Item>(5, 4);

    EXPECT_EQ(pool.chunk_size(), 4u);
    EXPECT_EQ(pool.capacity(), 8u);
    EXPECT_EQ(pool.available(), 8u);
    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPoolTest, NormalizesZeroChunkSizeToOne) {
    auto pool = make_object_pool<Item>(0, 0);

    EXPECT_EQ(pool.chunk_size(), 1u);

    auto first = pool.acquire();
    EXPECT_EQ(pool.capacity(), 1u);

    auto second = pool.acquire();
    EXPECT_NE(first.get(), second.get());
    EXPECT_EQ(pool.capacity(), 2u);
}

TEST(ObjectPoolTest, ConstructsObjectsWithMakePoolArguments) {
    auto pool = make_object_pool<ConstructedItem>(0, 2, 42);

    auto handle = pool.acquire();
    ASSERT_NE(handle.get(), nullptr);
    EXPECT_EQ(handle->value, 42);
}

TEST(ObjectPoolTest, ReturnsSlotWhenHandleIsDestroyed) {
    auto pool = make_object_pool<Item>(0, 64);

    Item* released{};
    {
        auto handle = pool.acquire();
        released = handle.get();
    }

    auto reacquired = pool.acquire();
    EXPECT_EQ(reacquired.get(), released);
}

TEST(ObjectPoolTest, TransfersHandleOwnershipOnMove) {
    auto pool = make_object_pool<Item>(0, 64);

    auto first = pool.acquire();
    ASSERT_TRUE(static_cast<bool>(first));

    auto second = std::move(first);
    EXPECT_FALSE(static_cast<bool>(first));
    EXPECT_TRUE(static_cast<bool>(second));
}

} // namespace ddcs::common
