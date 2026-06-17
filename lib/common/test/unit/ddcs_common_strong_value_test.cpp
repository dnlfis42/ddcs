#include "ddcs/common/strong_value.hpp"

#include <cstdint>
#include <type_traits>
#include <unordered_set>

#include <gtest/gtest.h>

namespace ddcs::common {

namespace {

using FirstValue = StrongValue<struct FirstValueTag, std::uint64_t>;
using SecondValue = StrongValue<struct SecondValueTag, std::uint64_t>;

static_assert(!std::is_convertible_v<std::uint64_t, FirstValue>);
static_assert(!std::same_as<FirstValue, SecondValue>);

static_assert(FirstValue{}.value() == FirstValue::invalid);
static_assert(!FirstValue{}.valid());
static_assert(FirstValue{42}.value() == 42u);
static_assert(FirstValue{42}.valid());
static_assert(FirstValue{42} == FirstValue{42});
static_assert(FirstValue{42} != FirstValue{7});

consteval bool clear_makes_value_invalid() {
    FirstValue value{42};
    value.clear();
    return value.value() == FirstValue::invalid && !value.valid();
}

static_assert(clear_makes_value_invalid());

} // namespace

TEST(StrongValueTest, StartsInvalid) {
    FirstValue value;

    EXPECT_EQ(value.value(), 0u);
    EXPECT_FALSE(value.valid());
}

TEST(StrongValueTest, ReportsNonInvalidValueAsValid) {
    FirstValue value{42};

    EXPECT_EQ(value.value(), 42u);
    EXPECT_TRUE(value.valid());
}

TEST(StrongValueTest, ClearsValueToInvalid) {
    FirstValue value{42};

    value.clear();

    EXPECT_EQ(value.value(), FirstValue::invalid);
    EXPECT_FALSE(value.valid());
}

TEST(StrongValueTest, ComparesValuesWithSameTag) {
    EXPECT_EQ(FirstValue{42}, FirstValue{42});
    EXPECT_NE(FirstValue{42}, FirstValue{7});
}

TEST(StrongValueTest, HashesValuesWithSameTag) {
    std::unordered_set<FirstValue> values;
    values.insert(FirstValue{42});

    EXPECT_TRUE(values.contains(FirstValue{42}));
    EXPECT_FALSE(values.contains(FirstValue{7}));
}

} // namespace ddcs::common
