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

} // namespace

TEST(StrongValueTest, ReportsDefaultValueAsInvalid) {
    FirstValue value;

    EXPECT_EQ(value.value(), 0u);
    EXPECT_FALSE(value.valid());
}

TEST(StrongValueTest, ReportsNonDefaultValueAsValid) {
    FirstValue value{42};

    EXPECT_EQ(value.value(), 42u);
    EXPECT_TRUE(value.valid());
}

TEST(StrongValueTest, ResetsToDefaultValue) {
    FirstValue value{42};
    ASSERT_TRUE(value.valid());

    value.reset();
    EXPECT_EQ(value.value(), 0u);
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
