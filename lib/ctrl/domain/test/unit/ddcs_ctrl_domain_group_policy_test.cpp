#include "ddcs/ctrl/domain/group_policy.hpp"

#include "ddcs/device/mode.hpp"

#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::domain::DeviceThermalRegime;
using ddcs::ctrl::domain::GroupLoadRegime;
using ddcs::ctrl::domain::GroupPolicy;
using ddcs::ctrl::domain::GroupRule;
using ddcs::ctrl::domain::ThermalRule;
using ddcs::device::Mode;

// 밴드 20~80, busy=safe / idle=normal
GroupRule make_rule() {
    return GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal).value();
}

// 밴드 20~80 + thermal 75~90, busy=performance / idle=normal / 과열=safe (세 mode가 서로 다름)
GroupRule make_thermal_rule() {
    return GroupRule::create(
               80.0, 20.0, Mode::performance, Mode::normal,
               ThermalRule{.hot_temp = 90.0, .cool_temp = 75.0, .hot_mode = Mode::safe}
    )
        .value();
}

TEST(GroupRuleTest, MakesRuleWhenLowBelowHigh) {
    auto const rule = GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal);

    ASSERT_TRUE(rule.has_value());
    EXPECT_EQ(rule->busy_load(), 80.0);
    EXPECT_EQ(rule->idle_load(), 20.0);
    EXPECT_EQ(rule->busy_mode(), Mode::safe);
    EXPECT_EQ(rule->idle_mode(), Mode::normal);
}

TEST(GroupRuleTest, RejectsInvertedBand) {
    EXPECT_FALSE(GroupRule::create(20.0, 80.0, Mode::safe, Mode::normal).has_value());
}

TEST(GroupRuleTest, RejectsEqualThresholds) {
    EXPECT_FALSE(GroupRule::create(50.0, 50.0, Mode::safe, Mode::normal).has_value());
}

TEST(GroupPolicyTest, AppendsRulesPreservingInsertionOrder) {
    GroupPolicy p;
    p.set("a", GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal).value());
    p.set("b", GroupRule::create(90.0, 30.0, Mode::normal, Mode::performance).value());

    ASSERT_EQ(p.size(), 2u);
    std::vector<std::string> order;
    p.for_each([&](std::string const& group, GroupRule const&) { order.push_back(group); });
    EXPECT_EQ(order, (std::vector<std::string>{"a", "b"}));
}

TEST(GroupPolicyTest, SetUpdatesExistingGroupInPlace) {
    GroupPolicy p;
    p.set("a", GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal).value());
    // 같은 Group 갱신
    p.set("a", GroupRule::create(70.0, 10.0, Mode::performance, Mode::safe).value());

    ASSERT_EQ(p.size(), 1u); // append 아님
    p.for_each([](std::string const& group, GroupRule const& rule) {
        EXPECT_EQ(group, "a");
        EXPECT_EQ(rule.busy_load(), 70.0); // 갱신 반영
        EXPECT_EQ(rule.busy_mode(), Mode::performance);
    });
}

TEST(GroupRuleTest, NoThermalByDefault) {
    auto const rule = GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal);
    ASSERT_TRUE(rule.has_value());
    EXPECT_FALSE(rule->thermal().has_value());
}

TEST(GroupRuleTest, MakesThermalRuleWhenResumeBelowHigh) {
    auto const rule = GroupRule::create(
        80.0, 20.0, Mode::performance, Mode::normal,
        ThermalRule{.hot_temp = 90.0, .cool_temp = 75.0, .hot_mode = Mode::safe}
    );
    ASSERT_TRUE(rule.has_value());
    ASSERT_TRUE(rule->thermal().has_value());
    EXPECT_EQ(rule->thermal()->hot_temp, 90.0);
    EXPECT_EQ(rule->thermal()->cool_temp, 75.0);
    EXPECT_EQ(rule->thermal()->hot_mode, Mode::safe);
}

TEST(GroupRuleTest, RejectsInvertedThermalBand) {
    EXPECT_FALSE(
        GroupRule::create(
            80.0, 20.0, Mode::performance, Mode::normal,
            ThermalRule{.hot_temp = 75.0, .cool_temp = 90.0, .hot_mode = Mode::safe}
        )
            .has_value()
    );
}

TEST(GroupRuleTest, RejectsNanThresholds) {
    // 부정형 비교 !(low < high)가 NaN을 자동 거부한다. low >= high로 고치면 깨지는 계약.
    double const nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(GroupRule::create(nan, 20.0, Mode::safe, Mode::normal).has_value());
    EXPECT_FALSE(GroupRule::create(80.0, nan, Mode::safe, Mode::normal).has_value());
    EXPECT_FALSE(
        GroupRule::create(
            80.0, 20.0, Mode::safe, Mode::normal,
            ThermalRule{.hot_temp = nan, .cool_temp = 75.0, .hot_mode = Mode::safe}
        )
            .has_value()
    );
}

TEST(GroupRuleTest, NextRegimeTripsToBusyAboveHighLoad) {
    auto const rule = make_rule();
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::unknown, 85.0), GroupLoadRegime::busy);
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::idle, 85.0), GroupLoadRegime::busy);
}

TEST(GroupRuleTest, NextRegimeReturnsToIdleBelowLowLoad) {
    auto const rule = make_rule();
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::busy, 15.0), GroupLoadRegime::idle);
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::unknown, 15.0), GroupLoadRegime::idle);
}

TEST(GroupRuleTest, NextRegimeHoldsPreviousInDeadband) {
    auto const rule = make_rule();
    // 같은 avg 50이라도 직전 판정에 따라 답이 다르다 (히스테리시스의 기억)
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::busy, 50.0), GroupLoadRegime::busy);
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::idle, 50.0), GroupLoadRegime::idle);
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::unknown, 50.0), GroupLoadRegime::unknown);
    // 경계값은 초과/미만이 아니라서 데드밴드에 속한다
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::idle, 80.0), GroupLoadRegime::idle);
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::busy, 20.0), GroupLoadRegime::busy);
}

TEST(GroupRuleTest, NextRegimeHoldsOnNanAverage) {
    auto const rule = make_rule();
    double const nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::busy, nan), GroupLoadRegime::busy);
    EXPECT_EQ(rule.next_regime(GroupLoadRegime::unknown, nan), GroupLoadRegime::unknown);
}

TEST(GroupRuleTest, NextThermalTripsAndReleasesWithHysteresis) {
    auto const rule = make_thermal_rule();
    EXPECT_EQ(
        rule.next_thermal(DeviceThermalRegime::cool, 95.0), DeviceThermalRegime::hot
    ); // 트립 (90 초과)
    EXPECT_EQ(
        rule.next_thermal(DeviceThermalRegime::hot, 80.0), DeviceThermalRegime::hot
    ); // 데드밴드(75~90) 유지
    EXPECT_EQ(
        rule.next_thermal(DeviceThermalRegime::cool, 80.0), DeviceThermalRegime::cool
    ); // 데드밴드 유지
    EXPECT_EQ(
        rule.next_thermal(DeviceThermalRegime::hot, 70.0), DeviceThermalRegime::cool
    ); // 복귀 (75 미만)
}

TEST(GroupRuleTest, NextThermalStaysCoolWithoutThermalRule) {
    // thermal 없는 룰(리로드로 제거된 경우 포함)은 hot latch를 즉시 해제한다
    auto const rule = make_rule();
    EXPECT_EQ(rule.next_thermal(DeviceThermalRegime::hot, 200.0), DeviceThermalRegime::cool);
    EXPECT_EQ(rule.next_thermal(DeviceThermalRegime::cool, 200.0), DeviceThermalRegime::cool);
}

TEST(GroupPolicyTest, ContainsKnownGroupsOnly) {
    GroupPolicy p;
    EXPECT_FALSE(p.contains("a")); // 빈 정책
    p.set("a", make_rule());
    EXPECT_TRUE(p.contains("a"));
    EXPECT_FALSE(p.contains("b"));
}

} // namespace
