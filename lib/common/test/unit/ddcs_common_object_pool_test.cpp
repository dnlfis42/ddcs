#include "ddcs/common/object_pool.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace ddcs::common {

namespace {

struct Item {
    void reset() noexcept {}
};

} // namespace

TEST(ObjectPoolTest, AcquireRoundtrip) {
    auto pool = make_pool<Item>(0, 64);
    EXPECT_EQ(pool.in_use(), 0u);

    {
        auto h = pool.acquire();
        EXPECT_EQ(pool.in_use(), 1u);
        ASSERT_NE(h.get(), nullptr);
    }

    EXPECT_EQ(pool.in_use(), 0u);
}

TEST(ObjectPoolTest, LifoOrder) {
    auto pool = make_pool<Item>(0, 64);

    auto h1 = pool.acquire();
    auto h2 = pool.acquire();

    Item* p1 = h1.get();
    Item* p2 = h2.get();

    h2.reset();
    h1.reset();

    auto h3 = pool.acquire();
    auto h4 = pool.acquire();

    EXPECT_EQ(h3.get(), p1);
    EXPECT_EQ(h4.get(), p2);
}

TEST(ObjectPoolTest, GrowsOnDemand) {
    auto pool = make_pool<Item>(0, 4);

    EXPECT_EQ(pool.capacity(), 0u);

    auto h1 = pool.acquire();
    EXPECT_EQ(pool.capacity(), 4u);

    auto h2 = pool.acquire();
    auto h3 = pool.acquire();
    auto h4 = pool.acquire();
    auto h5 = pool.acquire();

    EXPECT_EQ(pool.capacity(), 8u);
}

TEST(ObjectPoolTest, InitialCapacityRoundUp) {
    auto pool = make_pool<Item>(5, 4);
    EXPECT_EQ(pool.capacity(), 8u);
}

TEST(ObjectPoolTest, HandleDtorReuse) {
    auto pool = make_pool<Item>(0, 64);

    auto h1 = pool.acquire();
    Item* p1 = h1.get();
    h1.reset();

    auto h2 = pool.acquire();
    EXPECT_EQ(h2.get(), p1);
}

TEST(ObjectPoolTest, MoveOnly) {
    auto pool = make_pool<Item>(0, 64);

    auto h1 = pool.acquire();
    ASSERT_TRUE(static_cast<bool>(h1));

    auto h2 = std::move(h1);
    EXPECT_FALSE(static_cast<bool>(h1));
    EXPECT_TRUE(static_cast<bool>(h2));
}

} // namespace ddcs::common
