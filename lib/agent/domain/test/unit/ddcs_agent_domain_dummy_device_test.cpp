#include "ddcs/agent/domain/dummy_device.hpp"

#include "ddcs/device/command.hpp"
#include "ddcs/device/mode.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::agent::domain::DummyDevice;
using ddcs::device::Mode;
using ddcs::device::SetMode;

TEST(DummyDeviceTest, QueryReflectsInitialMode) {
    DummyDevice dev{Mode::safe};
    EXPECT_EQ(dev.query().mode, Mode::safe);
}

TEST(DummyDeviceTest, DefaultsToSafeMode) {
    DummyDevice dev{};
    EXPECT_EQ(dev.mode(), Mode::safe);
}

TEST(DummyDeviceTest, ApplySetModeUpdatesMode) {
    DummyDevice dev{Mode::safe};
    EXPECT_TRUE(dev.apply(SetMode{.mode = Mode::performance}));
    EXPECT_EQ(dev.query().mode, Mode::performance);
    EXPECT_EQ(dev.mode(), Mode::performance);
}

TEST(DummyDeviceTest, ApplyAcceptsSameMode) {
    DummyDevice dev{Mode::normal};
    EXPECT_TRUE(dev.apply(SetMode{.mode = Mode::normal}));
    EXPECT_EQ(dev.query().mode, Mode::normal);
}

TEST(DummyDeviceTest, SetModeHelperUpdatesMode) {
    DummyDevice dev{Mode::safe};
    dev.set_mode(Mode::performance);
    EXPECT_EQ(dev.mode(), Mode::performance);
}

TEST(DummyDeviceTest, QueryReportsSetLoadAndTemp) {
    DummyDevice dev{Mode::normal};
    dev.set_load(75.5);
    dev.set_temp(42.0);
    auto const st = dev.query();
    EXPECT_EQ(st.mode, Mode::normal);
    EXPECT_EQ(st.load, 75.5);
    EXPECT_EQ(st.temp, 42.0);
}

} // namespace
