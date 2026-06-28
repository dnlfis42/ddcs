#include "ddcs/agent/domain/simulated_device.hpp"

#include "ddcs/device/mode.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::agent::domain::SimulatedDevice;
using ddcs::device::Mode;

TEST(SimulatedDeviceTest, DefaultsToNormalMode) {
    SimulatedDevice dev{};
    EXPECT_EQ(dev.mode(), Mode::normal);
}

TEST(SimulatedDeviceTest, ApplySetModeUpdatesMode) {
    SimulatedDevice dev{{}, Mode::normal};
    EXPECT_TRUE(dev.apply(Mode::performance));
    EXPECT_EQ(dev.mode(), Mode::performance);
    EXPECT_EQ(dev.query().mode, Mode::performance);
}

// load는 mode-구동: performance면 빠지고 safe면 쌓인다.
TEST(SimulatedDeviceTest, LoadDrainsInPerformanceBuildsInSafe) {
    SimulatedDevice::Config c{};
    c.load_noise = 0.0; // 결정적
    c.load_initial = 50.0;
    SimulatedDevice dev{{}, Mode::performance, c};

    double const after_perf = dev.query().load; // 50 + (-4)
    EXPECT_LT(after_perf, 50.0);

    dev.apply(Mode::safe);
    double const after_safe = dev.query().load; // +4
    EXPECT_GT(after_safe, after_perf);
}

// temp는 mode-구동: performance면 오르고 safe면 빠르게 내려간다.
TEST(SimulatedDeviceTest, TempRisesInPerformanceCoolsInSafe) {
    SimulatedDevice::Config c{};
    c.temp_noise = 0.0;
    c.temp_initial = 60.0;
    SimulatedDevice dev{{}, Mode::performance, c};

    double const hot = dev.query().temp; // 60 + 6
    EXPECT_GT(hot, 60.0);

    dev.apply(Mode::safe);
    double const cooled = dev.query().temp; // -8
    EXPECT_LT(cooled, hot);
}

// rate가 초당이라 tick_seconds가 작으면 한 query당 변화도 작다(sampling과 decouple).
TEST(SimulatedDeviceTest, RateScalesWithTick) {
    SimulatedDevice::Config c{};
    c.load_noise = 0.0;
    c.load_initial = 50.0;
    c.tick_seconds = 0.5;
    SimulatedDevice dev{{}, Mode::performance, c};
    double const after = dev.query().load; // 50 + (-4 * 0.5) = 48
    EXPECT_DOUBLE_EQ(after, 48.0);
}

// clamp: load는 [0,100], temp는 [ambient, max] 밖으로 안 나간다.
TEST(SimulatedDeviceTest, StatesClampToRange) {
    SimulatedDevice::Config c{};
    c.load_noise = 0.0;
    c.temp_noise = 0.0;
    c.load_initial = 3.0;
    c.temp_initial = 40.0;
    c.temp_ambient = 35.0;
    SimulatedDevice dev{{}, Mode::safe, c}; // load 쌓임(+4), temp 냉각(-8)
    double load = 0.0;
    double temp = 0.0;
    for (int i = 0; i < 100; ++i) {
        auto const s = dev.query();
        load = s.load;
        temp = s.temp;
    }
    EXPECT_LE(load, 100.0); // safe라 100까지 쌓이고 clamp
    EXPECT_GE(load, 0.0);
    EXPECT_GE(temp, 35.0); // ambient 아래로 안 감
}

// noise=0이면 같은 config 두 device가 완전히 동일(결정적).
TEST(SimulatedDeviceTest, NoiseDisabledIsDeterministic) {
    SimulatedDevice::Config c{};
    c.load_noise = 0.0;
    c.temp_noise = 0.0;
    SimulatedDevice a{{}, Mode::normal, c};
    SimulatedDevice b{{}, Mode::normal, c};
    for (int i = 0; i < 5; ++i) {
        auto const sa = a.query();
        auto const sb = b.query();
        EXPECT_DOUBLE_EQ(sa.load, sb.load);
        EXPECT_DOUBLE_EQ(sa.temp, sb.temp);
    }
}

} // namespace
