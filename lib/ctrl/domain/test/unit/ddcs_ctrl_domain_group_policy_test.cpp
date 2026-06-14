#include "ddcs/ctrl/domain/group_policy.hpp"

#include "ddcs/device/mode.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::domain::GroupPolicy;
using ddcs::ctrl::domain::GroupRule;
using ddcs::device::Mode;

TEST(GroupRuleTest, MakesRuleWhenLowBelowHigh) {
    auto const rule = GroupRule::try_make(80.0, 20.0, Mode::safe, Mode::normal);

    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->high_load(), 80.0);
    EXPECT_EQ(rule->low_load(), 20.0);
    EXPECT_EQ(rule->busy_mode(), Mode::safe);
    EXPECT_EQ(rule->idle_mode(), Mode::normal);
}

TEST(GroupRuleTest, RejectsInvertedBand) {
    EXPECT_FALSE(GroupRule::try_make(20.0, 80.0, Mode::safe, Mode::normal).has_value());
}

TEST(GroupRuleTest, RejectsEqualThresholds) {
    EXPECT_FALSE(GroupRule::try_make(50.0, 50.0, Mode::safe, Mode::normal).has_value());
}

TEST(GroupPolicyTest, AppendsRulesPreservingInsertionOrder) {
    GroupPolicy p;
    p.set("a", GroupRule::try_make(80.0, 20.0, Mode::safe, Mode::normal).value());
    p.set("b", GroupRule::try_make(90.0, 30.0, Mode::normal, Mode::performance).value());

    ASSERT_EQ(p.size(), 2u);
    std::vector<std::string> order;
    p.for_each([&](std::string const& group, GroupRule const&) { order.push_back(group); });
    EXPECT_EQ(order, (std::vector<std::string>{"a", "b"}));
}

TEST(GroupPolicyTest, SetUpdatesExistingGroupInPlace) {
    GroupPolicy p;
    p.set("a", GroupRule::try_make(80.0, 20.0, Mode::safe, Mode::normal).value());
    p.set("a", GroupRule::try_make(70.0, 10.0, Mode::performance, Mode::safe).value()); // 같은 그룹 갱신

    ASSERT_EQ(p.size(), 1u); // append 아님
    p.for_each([](std::string const& group, GroupRule const& rule) {
        EXPECT_EQ(group, "a");
        EXPECT_EQ(rule.high_load(), 70.0); // 갱신 반영
        EXPECT_EQ(rule.busy_mode(), Mode::performance);
    });
}

} // namespace
