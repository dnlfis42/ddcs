#pragma once

#include "ddcs/ctrl/port/timer/timer_id.hpp"

#include <chrono>

namespace ddcs::ctrl::port::timer {

class TimerHandler;

class TimerScheduler {
public:
    virtual ~TimerScheduler() = default;

public:
    [[nodiscard]] virtual TimerId schedule(std::chrono::nanoseconds delay, TimerHandler& handler) = 0;
    virtual void cancel(TimerId id) = 0;
};

} // namespace ddcs::ctrl::port::timer
