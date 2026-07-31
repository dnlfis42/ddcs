#include "ddcs/ctrl/app/device/group_aggregate.hpp"

#include "ddcs/ctrl/app/device/port/active_devices.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/domain/group_policy.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"

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
using ddcs::ctrl::app::device::port::ActiveDevices;
using ddcs::ctrl::domain::DeviceId;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::ctrl::domain::GroupPolicy;
using ddcs::ctrl::domain::GroupRule;
using ddcs::device::Mode;
using ddcs::device::Status;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

// 고정된 active 집합을 내주는 대역.
class FakeActiveDevices final : public ActiveDevices {
public:
    std::vector<DeviceId> active;

    void for_each_active(std::function<void(DeviceId)> const& fn) override {
        for (auto const id : active) {
            fn(id);
        }
    }
};

struct AggregateFixture {
    FakeActiveDevices active_devices;
    DeviceRegistry devices;
    GroupPolicy policy;

    DeviceId enroll(std::uint8_t seed, std::string group, double load, double temp, Mode mode) {
        DeviceId const id = make_device_id(seed);
        devices.enroll(id, std::move(group));
        EXPECT_TRUE(devices.update_status(id, Status{.mode = mode, .load = load, .temp = temp}));
        active_devices.active.push_back(id);
        return id;
    }

    static GroupPolicy sensors_policy() {
        GroupPolicy p;
        p.set("sensors", GroupRule::create(80.0, 20.0, Mode::safe, Mode::normal).value());
        return p;
    }
};

TEST(GroupAggregateTest, AggregatesActiveDevicesOfPolicyGroups) {
    AggregateFixture f;
    f.policy = AggregateFixture::sensors_policy();
    f.enroll(0x01, "sensors", 10.0, 40.0, Mode::normal);
    f.enroll(0x02, "sensors", 30.0, 60.0, Mode::performance);
    f.enroll(0x03, "pumps", 99.0, 99.0, Mode::safe); // 정책 밖 group은 제외

    auto const groups = aggregate_groups(f.active_devices, f.devices, f.policy);

    ASSERT_EQ(groups.size(), 1u);
    auto const& agg = groups.at("sensors");
    EXPECT_EQ(agg.device_count, 2u);
    EXPECT_EQ(agg.load_sum, 40.0);
    EXPECT_EQ(agg.temp_sum, 100.0);
    EXPECT_EQ(agg.by_mode[ddcs::device::encode_mode(Mode::normal)], 1u);
    EXPECT_EQ(agg.by_mode[ddcs::device::encode_mode(Mode::performance)], 1u);
    EXPECT_EQ(agg.by_mode[ddcs::device::encode_mode(Mode::safe)], 0u);
}

TEST(GroupAggregateTest, SkipsUnobservedDevices) {
    AggregateFixture f;
    f.policy = AggregateFixture::sensors_policy();
    f.enroll(0x01, "sensors", 10.0, 40.0, Mode::normal);
    // 등록만 되고 아직 Status를 보고하지 않은 device는 집계에서 빠진다
    DeviceId const fresh = make_device_id(0x02);
    f.devices.enroll(fresh, "sensors");
    f.active_devices.active.push_back(fresh);

    auto const groups = aggregate_groups(f.active_devices, f.devices, f.policy);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.at("sensors").device_count, 1u);
    EXPECT_EQ(groups.at("sensors").load_sum, 10.0);
}

TEST(GroupAggregateTest, SkipsRosterEntriesWithoutShadow) {
    AggregateFixture f;
    f.policy = AggregateFixture::sensors_policy();
    f.enroll(0x01, "sensors", 10.0, 40.0, Mode::normal);
    f.active_devices.active.push_back(make_device_id(0x77)); // 등록부에 Shadow 없음

    auto const groups = aggregate_groups(f.active_devices, f.devices, f.policy);

    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups.at("sensors").device_count, 1u);
}

TEST(GroupAggregateTest, EmptyPolicyAggregatesNothing) {
    AggregateFixture f; // 빈 정책
    f.enroll(0x01, "sensors", 10.0, 40.0, Mode::normal);

    auto const groups = aggregate_groups(f.active_devices, f.devices, f.policy);

    EXPECT_TRUE(groups.empty());
}

} // namespace
