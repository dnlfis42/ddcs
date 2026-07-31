#include "ddcs/ctrl/domain/device_registry.hpp"

#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using namespace ddcs::ctrl::domain;
using ddcs::common::Uuid;
using ddcs::device::Mode;
using ddcs::device::Status;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return Uuid{b};
}

TEST(DeviceRegistryTest, CreatesShadowForUnknownUuid) {
    DeviceRegistry reg;
    auto const u = make_uuid(1);
    reg.enroll(u, "sensors");
    DeviceShadow const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->id, u); // uuid가 곧 DeviceId (서로게이트 발급 없음)
    EXPECT_EQ(d->group, "sensors");
    EXPECT_EQ(reg.size(), 1u);
}

TEST(DeviceRegistryTest, CreatesShadowWithoutObservation) {
    // 갓 등록한 Shadow는 미관측이다. 기본값 관측을 지어내지 않는다.
    DeviceRegistry reg;
    auto const u = make_uuid(12);
    reg.enroll(u, "sensors");
    ASSERT_NE(reg.find(u), nullptr);
    EXPECT_FALSE(reg.find(u)->status.has_value());
}

TEST(DeviceRegistryTest, CreatesSeparateShadowsForDistinctUuids) {
    DeviceRegistry reg;
    reg.enroll(make_uuid(3), "a");
    reg.enroll(make_uuid(4), "b");
    EXPECT_EQ(reg.size(), 2u);
}

TEST(DeviceRegistryTest, FindsByUuid) {
    DeviceRegistry reg;
    auto const u = make_uuid(5);
    reg.enroll(u, "sensors");
    DeviceShadow const* found = reg.find(u);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, u);
    EXPECT_EQ(reg.find(make_uuid(6)), nullptr);
}

TEST(DeviceRegistryTest, RefreshesGroupOnReRegister) {
    DeviceRegistry reg;
    auto const u = make_uuid(9);
    reg.enroll(u, "g1");
    reg.enroll(u, "g2"); // 재등록: 선언된 group 갱신
    DeviceShadow const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(d->group, "g2");
    EXPECT_EQ(reg.size(), 1u);
}

TEST(DeviceRegistryTest, PreservesStatusOnReRegister) {
    // 재접속의 핵심 불변식: 재등록해도 직전 관측은 남는다
    DeviceRegistry reg;
    auto const u = make_uuid(11);
    reg.enroll(u, "sensors");
    ASSERT_TRUE(reg.update_status(u, Status{.mode = Mode::normal, .load = 70.0, .temp = 60.0}));

    reg.enroll(u, "sensors"); // 재등록
    DeviceShadow const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    ASSERT_TRUE(d->status.has_value());
    EXPECT_EQ(d->status->mode, Mode::normal);
    EXPECT_EQ(d->status->load, 70.0);
    EXPECT_EQ(reg.size(), 1u);
}

TEST(DeviceRegistryTest, UpdatesStatus) {
    DeviceRegistry reg;
    auto const u = make_uuid(10);
    reg.enroll(u, "sensors");
    EXPECT_TRUE(reg.update_status(u, Status{.mode = Mode::performance, .load = 42.0, .temp = 55.5})
    );
    DeviceShadow const* d = reg.find(u);
    ASSERT_NE(d, nullptr);
    ASSERT_TRUE(d->status.has_value());
    EXPECT_EQ(d->status->mode, Mode::performance);
    EXPECT_EQ(d->status->load, 42.0);
    EXPECT_EQ(d->status->temp, 55.5);
}

TEST(DeviceRegistryTest, RejectsStatusForUnknownId) {
    DeviceRegistry reg;
    // 등록 없이 온 보고는 false로 알린다 (조용한 무시가 아님)
    EXPECT_FALSE(
        reg.update_status(make_uuid(201), Status{.mode = Mode::normal, .load = 1.0, .temp = 1.0})
    );
    EXPECT_EQ(reg.size(), 0u);
}

} // namespace
