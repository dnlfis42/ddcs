#include "ddcs/io/timer_scheduler.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

using ddcs::io::Reactor;
using ddcs::io::TimerHandler;
using ddcs::io::TimerId;
using ddcs::io::TimerScheduler;

class RecordingTimer : public TimerHandler {
public:
    void on_expired(TimerId id) override {
        fired_.push_back(id);
    }

    std::vector<TimerId> fired_;
};

TEST(TimerSchedulerTest, DispatchesTimerThroughReactor) {
    Reactor reactor;
    TimerScheduler timers{reactor};
    RecordingTimer handler;

    timers.start();
    TimerId const id = timers.schedule(5ms, handler);
    reactor.run_once(1000ms);

    ASSERT_EQ(handler.fired_.size(), 1u);
    EXPECT_EQ(handler.fired_[0], id);
}

TEST(TimerSchedulerTest, SkipsCancelledTimer) {
    Reactor reactor;
    TimerScheduler timers{reactor};
    RecordingTimer handler;

    timers.start();
    TimerId const id = timers.schedule(5ms, handler);
    timers.cancel(id);
    reactor.run_once(50ms);

    EXPECT_TRUE(handler.fired_.empty());
}

TEST(TimerSchedulerTest, RearmsTimerFdWhenNextTimerIsCancelled) {
    ddcs::common::ManualClock clock;
    Reactor reactor;
    TimerScheduler timers{reactor, clock};
    RecordingTimer handler;

    timers.start();
    TimerId const first_id = timers.schedule(5ms, handler);
    TimerId const second_id = timers.schedule(20ms, handler);
    timers.cancel(first_id);

    clock.advance(5ms);
    timers.dispatch_expired();
    EXPECT_TRUE(handler.fired_.empty());

    clock.advance(15ms);
    timers.dispatch_expired();

    ASSERT_EQ(handler.fired_.size(), 1u);
    EXPECT_EQ(handler.fired_[0], second_id);
}

TEST(TimerSchedulerTest, DispatchesExpiredTimersWithInjectedClock) {
    ddcs::common::ManualClock clock;
    Reactor reactor;
    TimerScheduler timers{reactor, clock};
    RecordingTimer handler;

    TimerId const id = timers.schedule(5ms, handler);
    timers.dispatch_expired();
    EXPECT_TRUE(handler.fired_.empty());

    clock.advance(5ms);
    timers.dispatch_expired();

    ASSERT_EQ(handler.fired_.size(), 1u);
    EXPECT_EQ(handler.fired_[0], id);
}

TEST(TimerSchedulerTest, StopsSafelyFromCallback) {
    Reactor reactor;
    TimerScheduler timers{reactor};

    class StopTimer : public TimerHandler {
    public:
        explicit StopTimer(TimerScheduler& scheduler_ref)
            : scheduler{scheduler_ref} {}

        void on_expired(TimerId) override {
            ++count;
            scheduler.stop();
        }

        int count = 0;
        TimerScheduler& scheduler;
    } handler{timers};

    timers.start();
    (void)timers.schedule(1ms, handler);
    reactor.run_once(1000ms);

    EXPECT_EQ(handler.count, 1);
}

} // namespace
