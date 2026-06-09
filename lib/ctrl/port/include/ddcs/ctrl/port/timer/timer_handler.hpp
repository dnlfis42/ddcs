#pragma once

#include "ddcs/ctrl/port/timer/timer_id.hpp"

namespace ddcs::ctrl::port::timer {

class TimerHandler {
public:
    virtual ~TimerHandler() = default;

public:
    virtual void on_expired(TimerId id) = 0;
};

} // namespace ddcs::ctrl::port::timer
