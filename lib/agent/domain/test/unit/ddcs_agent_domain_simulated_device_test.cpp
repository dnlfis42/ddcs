#include "ddcs/agent/domain/simulated_device.hpp"

#include "ddcs/device/mode.hpp"
#include "ddcs/proto/cmd/command.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::agent::domain::SimulatedDevice;
using ddcs::device::Mode;
using ddcs::proto::cmd::SetMode;

TEST(SimulatedDeviceTest, DefaultsToNormalMode) {
    SimulatedDevice dev{};
    EXPECT_EQ(dev.mode(), Mode::normal);
}

TEST(SimulatedDeviceTest, QueryAdvancesLoadEachCall) {
    SimulatedDevice dev{};
    double const a = dev.query().load;
    double const b = dev.query().load;
    EXPECT_NE(a, b);
}

TEST(SimulatedDeviceTest, LoadStaysWithinBand) {
    SimulatedDevice::Config cfg{.baseline = 55.0, .amplitude = 45.0, .step = 0.7, .temp = 40.0};
    SimulatedDevice dev{Mode::normal, cfg};
    for (int i = 0; i < 200; ++i) {
        double const load = dev.query().load;
        EXPECT_GE(load, cfg.baseline - cfg.amplitude - 1e-9);
        EXPECT_LE(load, cfg.baseline + cfg.amplitude + 1e-9);
    }
}

TEST(SimulatedDeviceTest, CrossesHighAndLowOverCycle) {
    SimulatedDevice dev{}; // 기본 band [10, 100]
    bool above_high = false;
    bool below_low = false;
    for (int i = 0; i < 200; ++i) {
        double const load = dev.query().load;
        if (load > 70.0) {
            above_high = true;
        }
        if (load < 30.0) {
            below_low = true;
        }
    }
    EXPECT_TRUE(above_high);
    EXPECT_TRUE(below_low);
}

TEST(SimulatedDeviceTest, ApplySetModeUpdatesMode) {
    SimulatedDevice dev{Mode::normal};
    EXPECT_TRUE(dev.apply(SetMode{.mode = Mode::performance}));
    EXPECT_EQ(dev.mode(), Mode::performance);
    EXPECT_EQ(dev.query().mode, Mode::performance);
}

TEST(SimulatedDeviceTest, QueryReportsConfiguredTemp) {
    SimulatedDevice::Config cfg{};
    cfg.temp = 42.5;
    SimulatedDevice dev{Mode::normal, cfg};
    EXPECT_EQ(dev.query().temp, 42.5);
}

} // namespace
