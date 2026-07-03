#include "ddcs/ctrl/app/device/group_aggregate.hpp"

#include "ddcs/ctrl/app/device/port/device_roster.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/ctrl/domain/status.hpp"
#include "ddcs/device/mode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::app::device::aggregate_groups;
using ddcs::ctrl::app::device::port::DeviceRoster;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::domain::GroupPolicy;
using ddcs::ctrl::domain::GroupRule;
using ddcs::device::Mode;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

// 고정된 active 집합을 내주는 대역.
class FakeDeviceRoster final : public DeviceRoster {
public:
    std::vector<DeviceId> active;

    void for_each_active(std::function<void(DeviceId)> const& fn) override {
        for (auto const id : active) {
            fn(id);
        }
    }
};

struct AggregateFixture {
    FakeDeviceRoster roster;
    DeviceRegistry devices;
    GroupPolicy policy;

    DeviceId enroll(std::uint8_t seed, std::string group, double load, double temp, Mode mode) {
        DeviceId const id = make_device_id(seed);
        devices.find_or_create(id);
        devices.set_group(id, std::move(group));
        devices.update_status(
            id, ddcs::ctrl::domain::Status{.mode = mode, .load = load, .temp = temp}
        );
        roster.active.push_back(id);
        return id;
    }

    static GroupPolicy sensors_policy() {
        GroupPolicy p;
        p.set("sensors", GroupRule::try_make(80.0, 20.0, Mode::safe, Mode::normal).value());
        return p;
    }
};

TEST(GroupAggregateTest, AggregatesActiveDevicesOfPolicyGroups) {
    AggregateFixture f;
    f.policy = AggregateFixture::sensors_policy();
    f.enroll(0x01, "sensors", 10.0, 40.0, Mode::normal);
    f.enroll(0x02, "sensors", 30.0, 60.0, Mode::performance);
    f.enroll(0x03, "pumps", 99.0, 99.0, Mode::safe); // 정책 밖 group은 제외

    auto const groups = aggregate_groups(f.roster, f.devices, f.policy);

    ASSERT_EQ(groups.size(), 1u);
    auto const& agg = groups.at("sensors");
    EXPECT_EQ(agg.devices, 2u);
    EXPECT_EQ(agg.load_sum, 40.0);
    EXPECT_EQ(agg.temp_sum, 100.0);
    EXPECT_EQ(agg.by_mode[ddcs::device::encode_mode(Mode::normal)], 1u);
    EXPECT_EQ(agg.by_mode[ddcs::device::encode_mode(Mode::performance)], 1u);
    EXPECT_EQ(agg.by_mode[ddcs::device::encode_mode(Mode::safe)], 0u);
}

TEST(GroupAggregateTest, SkipsRosterEntriesWithoutShadow) {
    AggregateFixture f;
    f.policy = AggregateFixture::sensors_policy();
    f.enroll(0x01, "sensors", 10.0, 40.0, Mode::normal);
    f.roster.active.push_back(make_device_id(0x77)); // 등록부에 Shadow 없음

    auto const groups = aggregate_groups(f.roster, f.devices, f.policy);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.at("sensors").devices, 1u);
}

TEST(GroupAggregateTest, EmptyPolicyAggregatesNothing) {
    AggregateFixture f; // 빈 정책
    f.enroll(0x01, "sensors", 10.0, 40.0, Mode::normal);

    auto const groups = aggregate_groups(f.roster, f.devices, f.policy);

    EXPECT_TRUE(groups.empty());
}

} // namespace
