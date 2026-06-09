#include "ddcs/ctrl/port/timer/timer_handler.hpp"
#include "ddcs/ctrl/port/timer/timer_id.hpp"
#include "ddcs/ctrl/port/timer/timer_scheduler.hpp"

#include <chrono>

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::port::timer::TimerHandler;
using ddcs::ctrl::port::timer::TimerId;
using ddcs::ctrl::port::timer::TimerScheduler;

using namespace std::chrono_literals;

class MockTimerHandler final : public TimerHandler {
public:
    void on_expired(TimerId id) override { last_expired = id; }

public:
    TimerId last_expired{};
};

class MockTimerScheduler final : public TimerScheduler {
public:
    TimerId schedule(std::chrono::nanoseconds delay, TimerHandler& handler) override {
        scheduled_delay = delay;
        scheduled_handler = &handler;
        return TimerId{next_id++};
    }

    void cancel(TimerId id) override { cancelled_id = id; }

public:
    TimerHandler* scheduled_handler{nullptr};
    std::uint64_t next_id{1};
    std::chrono::nanoseconds scheduled_delay{};
    TimerId cancelled_id{};
};

} // namespace

TEST(PortTimerTest, ReportsDefaultTimerIdAsInvalid) {
    TimerId const id{};

    EXPECT_EQ(id.value(), 0u);
    EXPECT_FALSE(id.valid());
}

TEST(PortTimerTest, ReportsNonDefaultTimerIdAsValid) {
    TimerId const id{42};

    EXPECT_EQ(id.value(), 42u);
    EXPECT_TRUE(id.valid());
}

TEST(PortTimerTest, AcceptsTimerHandlerImplementation) {
    MockTimerHandler handler;
    TimerHandler& port = handler;

    port.on_expired(TimerId{7});

    EXPECT_EQ(handler.last_expired, TimerId{7});
}

TEST(PortTimerTest, AcceptsTimerSchedulerImplementation) {
    MockTimerHandler handler;
    MockTimerScheduler scheduler;
    TimerScheduler& port = scheduler;

    TimerId const id = port.schedule(15ms, handler);
    port.cancel(id);

    EXPECT_EQ(id, TimerId{1});
    EXPECT_EQ(scheduler.scheduled_delay, 15ms);
    EXPECT_EQ(scheduler.scheduled_handler, &handler);
    EXPECT_EQ(scheduler.cancelled_id, id);
}
