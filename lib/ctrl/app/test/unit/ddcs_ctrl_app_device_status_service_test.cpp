#include "ddcs/ctrl/app/device/status_service.hpp"

#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/device/mode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

namespace {

using ddcs::common::Uuid;
using ddcs::ctrl::app::device::StatusService;
using ddcs::ctrl::domain::DeviceRegistry;
using ddcs::device::Mode;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return Uuid{bytes};
}

TEST(StatusServiceTest, UpdateStatusUpdatesDeviceShadow) {
    DeviceRegistry devices;
    StatusService service{devices};
    auto const id = make_uuid(0xAA);
    devices.find_or_create(id);

    service.update_status(id, 2, 0.5, 41.0);

    auto const* dev = devices.find(id);
    ASSERT_NE(dev, nullptr);
    EXPECT_EQ(dev->status.mode, Mode::performance);
    EXPECT_DOUBLE_EQ(dev->status.load, 0.5);
    EXPECT_DOUBLE_EQ(dev->status.temp, 41.0);
}

TEST(StatusServiceTest, UnknownModeFallsBackToSafe) {
    DeviceRegistry devices;
    StatusService service{devices};
    auto const id = make_uuid(0xAA);
    devices.find_or_create(id);

    service.update_status(id, 99, 0.1, 30.0);

    EXPECT_EQ(devices.find(id)->status.mode, Mode::safe);
}

TEST(StatusServiceTest, UnknownDeviceIgnored) {
    DeviceRegistry devices;
    StatusService service{devices};

    service.update_status(make_uuid(0xAA), 1, 0.1, 30.0);

    EXPECT_EQ(devices.size(), 0u);
}

TEST(StatusServiceTest, IgnoresNonFiniteLoadKeepingLastGood) {
    DeviceRegistry devices;
    StatusService service{devices};
    auto const id = make_uuid(0xAA);
    devices.find_or_create(id);
    service.update_status(id, 1, 0.5, 40.0); // 직전 정상 보고

    service.update_status(id, 2, std::numeric_limits<double>::quiet_NaN(), 45.0);

    auto const* dev = devices.find(id);
    ASSERT_NE(dev, nullptr);
    EXPECT_EQ(dev->status.mode, Mode::normal); // 갱신 거부, 직전 mode 보존
    EXPECT_DOUBLE_EQ(dev->status.load, 0.5);
    EXPECT_DOUBLE_EQ(dev->status.temp, 40.0);
}

TEST(StatusServiceTest, IgnoresNonFiniteTempKeepingLastGood) {
    DeviceRegistry devices;
    StatusService service{devices};
    auto const id = make_uuid(0xAA);
    devices.find_or_create(id);
    service.update_status(id, 1, 0.3, 30.0); // 직전 정상 보고

    service.update_status(id, 2, 0.7, std::numeric_limits<double>::infinity());

    auto const* dev = devices.find(id);
    ASSERT_NE(dev, nullptr);
    EXPECT_DOUBLE_EQ(dev->status.load, 0.3);
    EXPECT_DOUBLE_EQ(dev->status.temp, 30.0);
}

} // namespace
