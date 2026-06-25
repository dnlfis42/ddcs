#include "ddcs/ctrl/app/device/registration_service.hpp"

#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::common::Uuid;
using ddcs::ctrl::app::device::RegistrationService;
using ddcs::ctrl::domain::DeviceRegistry;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return Uuid{bytes};
}

TEST(RegistrationServiceTest, EnrollCreatesDeviceAndSetsGroup) {
    DeviceRegistry devices;
    RegistrationService service{devices};

    auto const id = service.enroll(make_uuid(0xAA), "line-a");

    ASSERT_TRUE(id.valid());
    auto const* dev = devices.find(id);
    ASSERT_NE(dev, nullptr);
    EXPECT_EQ(dev->group, "line-a");
    EXPECT_EQ(devices.size(), 1u);
}

TEST(RegistrationServiceTest, EnrollSameUuidReusesDeviceAndUpdatesGroup) {
    DeviceRegistry devices;
    RegistrationService service{devices};

    auto const first = service.enroll(make_uuid(0xAA), "line-a");
    auto const second = service.enroll(make_uuid(0xAA), "line-b");

    EXPECT_EQ(first, second);
    EXPECT_EQ(devices.size(), 1u);
    EXPECT_EQ(devices.find(second)->group, "line-b");
}

TEST(RegistrationServiceTest, EnrollRejectsNilUuid) {
    DeviceRegistry devices;
    RegistrationService service{devices};

    auto const id = service.enroll(Uuid{}, "line-a");

    EXPECT_FALSE(id.valid());
    EXPECT_EQ(devices.size(), 0u);
}

} // namespace
