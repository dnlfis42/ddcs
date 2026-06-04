#include "ddcs/runtime/timer_source.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/runtime/reactor.hpp"
#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

using namespace std::chrono_literals;

namespace {

using ddcs::runtime::Reactor;
using ddcs::runtime::TimerHandler;
using ddcs::runtime::TimerId;
using ddcs::runtime::TimerSource;

class RecordingTimer : public TimerHandler {
public:
    std::vector<TimerId> fired;
    void on_timer(TimerId id) override { fired.push_back(id); }
};

} // namespace

TEST(TimerSourceTest, TimerFiresThroughReactor) {
    Reactor reactor;
    TimerSource timers{reactor};
    RecordingTimer handler;

    timers.start();
    TimerId const id = timers.schedule(5ms, &handler);
    reactor.run_once(1000ms);

    ASSERT_EQ(handler.fired.size(), 1u);
    EXPECT_EQ(handler.fired[0], id);
}

TEST(TimerSourceTest, CancelledTimerDoesNotFire) {
    Reactor reactor;
    TimerSource timers{reactor};
    RecordingTimer handler;

    timers.start();
    TimerId const id = timers.schedule(5ms, &handler);
    timers.cancel(id);
    reactor.run_once(50ms);

    EXPECT_TRUE(handler.fired.empty());
}

TEST(TimerSourceTest, ExpireDueUsesInjectedClock) {
    ddcs::common::ManualClock clock;
    Reactor reactor;
    TimerSource timers{reactor, clock};
    RecordingTimer handler;

    TimerId const id = timers.schedule(5ms, &handler);
    timers.expire_due();
    EXPECT_TRUE(handler.fired.empty());

    clock.advance(5ms);
    timers.expire_due();

    ASSERT_EQ(handler.fired.size(), 1u);
    EXPECT_EQ(handler.fired[0], id);
}
