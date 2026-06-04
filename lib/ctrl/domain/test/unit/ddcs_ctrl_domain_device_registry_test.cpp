#include "ddcs/ctrl/domain/device_registry.hpp"

#include "ddcs/common/uuid.hpp"

#include <gtest/gtest.h>

#include <array>

#include <cstddef>
#include <cstdint>

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

} // namespace

TEST(DeviceRegistryTest, CreatesNewDeviceForUnknownUuid) {
    DeviceRegistry reg;
    auto const u = make_uuid(1);
    Device const& d = reg.find_or_create(u);
    EXPECT_EQ(d.id, u);
    EXPECT_TRUE(d.id.valid());
    EXPECT_EQ(reg.size(), 1u);
}

TEST(DeviceRegistryTest, ReturnsSameIdForSameUuid) {
    DeviceRegistry reg;
    auto const u = make_uuid(2);
    DeviceId const id1 = reg.find_or_create(u).id;
    DeviceId const id2 = reg.find_or_create(u).id; // 재등록 -> 동일 id (identity persistence)
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
    Device const* found = reg.find(u);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, u);
    EXPECT_EQ(reg.find(make_uuid(6)), nullptr);
}

TEST(DeviceRegistryTest, FindsById) {
    DeviceRegistry reg;
    DeviceId const id = reg.find_or_create(make_uuid(7)).id;
    Device const* found = reg.find(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, id);
    EXPECT_EQ(reg.find(make_uuid(99)), nullptr);
}

TEST(DeviceRegistryTest, SetAttributesUpdatesGroupAndVersion) {
    DeviceRegistry reg;
    auto const u = make_uuid(8);
    DeviceId const id = reg.find_or_create(u).id;
    reg.set_attributes(id, "sensors", "1.2.3");
    Device const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->group, "sensors");
    EXPECT_EQ(d->version, "1.2.3");
}

TEST(DeviceRegistryTest, SetAttributesRefreshesOnReRegister) {
    DeviceRegistry reg;
    auto const u = make_uuid(9);
    DeviceId const id = reg.find_or_create(u).id;
    reg.set_attributes(id, "g1", "v1");
    reg.set_attributes(id, "g2", "v2"); // 재등록 -> 갱신
    Device const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->group, "g2");
    EXPECT_EQ(d->version, "v2");
}

TEST(DeviceRegistryTest, SetAttributesIgnoresUnknownId) {
    DeviceRegistry reg;
    reg.set_attributes(make_uuid(200), "g", "v"); // 미지 id -> no-op (크래시 없음)
    EXPECT_EQ(reg.size(), 0u);
}
