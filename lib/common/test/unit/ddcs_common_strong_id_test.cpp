#include "ddcs/common/strong_id.hpp"

#include <cstdint>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

namespace {

using ddcs::common::StrongId;

using FirstId = StrongId<struct FirstIdTag, std::uint64_t>;
using SecondId = StrongId<struct SecondIdTag, std::uint64_t>;

static_assert(!std::is_convertible_v<std::uint64_t, FirstId>);
static_assert(!std::same_as<FirstId, SecondId>);

static_assert(FirstId{}.get() == FirstId::invalid);
static_assert(!FirstId{}.valid());
static_assert(FirstId{42}.get() == 42u);
static_assert(FirstId{42}.valid());
static_assert(FirstId{42} == FirstId{42});
static_assert(FirstId{42} != FirstId{7});

consteval bool clear_makes_value_invalid() {
    FirstId value{42};
    value.clear();
    return value.get() == FirstId::invalid && !value.valid();
}

static_assert(clear_makes_value_invalid());

TEST(StrongIdTest, StartsInvalid) {
    FirstId value;

    EXPECT_EQ(value.get(), 0u);
    EXPECT_FALSE(value.valid());
}

TEST(StrongIdTest, ReportsNonInvalidValueAsValid) {
    FirstId value{42};

    EXPECT_EQ(value.get(), 42u);
    EXPECT_TRUE(value.valid());
}

TEST(StrongIdTest, ClearsValueToInvalid) {
    FirstId value{42};

    value.clear();

    EXPECT_EQ(value.get(), FirstId::invalid);
    EXPECT_FALSE(value.valid());
}

TEST(StrongIdTest, ComparesValuesWithSameTag) {
    EXPECT_EQ(FirstId{42}, FirstId{42});
    EXPECT_NE(FirstId{42}, FirstId{7});
}

TEST(StrongIdTest, HashesValuesWithSameTag) {
    std::unordered_set<FirstId> values;
    values.insert(FirstId{42});

    EXPECT_TRUE(values.contains(FirstId{42}));
    EXPECT_FALSE(values.contains(FirstId{7}));
}

} // namespace
