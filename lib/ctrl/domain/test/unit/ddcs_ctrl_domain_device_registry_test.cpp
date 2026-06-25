#include "ddcs/ctrl/domain/device_registry.hpp"

#include "ddcs/common/uuid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using namespace ddcs::ctrl::domain;
using ddcs::common::Uuid;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return Uuid{b};
}

TEST(DeviceRegistryTest, CreatesNewDeviceForUnknownUuid) {
    DeviceRegistry reg;
    auto const u = make_uuid(1);
    DeviceShadow const& d = reg.find_or_create(u);
    EXPECT_EQ(d.id, u);
    EXPECT_TRUE(d.id.valid());
    EXPECT_EQ(reg.size(), 1u);
}

TEST(DeviceRegistryTest, ReturnsSameIdForSameUuid) {
    DeviceRegistry reg;
    auto const u = make_uuid(2);
    DeviceId const id1 = reg.find_or_create(u).id;
    DeviceId const id2 = reg.find_or_create(u).id; // 재등록 시 동일 id (identity persistence)
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(reg.size(), 1u);
}

TEST(DeviceRegistryTest, AssignsDistinctIdsToDistinctUuids) {
    DeviceRegistry reg;
    DeviceId const a = reg.find_or_create(make_uuid(3)).id;
    DeviceId const b = reg.find_or_create(make_uuid(4)).id;
    EXPECT_NE(a, b);
    EXPECT_EQ(reg.size(), 2u);
}

TEST(DeviceRegistryTest, FindsByUuid) {
    DeviceRegistry reg;
    auto const u = make_uuid(5);
    reg.find_or_create(u);
    DeviceShadow const* found = reg.find(u);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, u);
    EXPECT_EQ(reg.find(make_uuid(6)), nullptr);
}

TEST(DeviceRegistryTest, FindsById) {
    DeviceRegistry reg;
    DeviceId const id = reg.find_or_create(make_uuid(7)).id;
    DeviceShadow const* found = reg.find(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, id);
    EXPECT_EQ(reg.find(make_uuid(99)), nullptr);
}

TEST(DeviceRegistryTest, SetsGroup) {
    DeviceRegistry reg;
    auto const u = make_uuid(8);
    DeviceId const id = reg.find_or_create(u).id;
    reg.set_group(id, "sensors");
    DeviceShadow const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->group, "sensors");
}

TEST(DeviceRegistryTest, RefreshesGroupOnReRegister) {
    DeviceRegistry reg;
    auto const u = make_uuid(9);
    DeviceId const id = reg.find_or_create(u).id;
    reg.set_group(id, "g1");
    reg.set_group(id, "g2"); // 재등록 시 갱신
    DeviceShadow const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->group, "g2");
}

TEST(DeviceRegistryTest, IgnoresUnknownIdWhenSettingGroup) {
    DeviceRegistry reg;
    reg.set_group(make_uuid(200), "g"); // 미지 id면 no-op (크래시 없음)
    EXPECT_EQ(reg.size(), 0u);
}

} // namespace
