#pragma once

#include "ddcs/runtime/timer_id.hpp"

namespace ddcs::runtime {

// TimerScheduler가 전달하는 논리 타이머 만료를 받는다.
class TimerHandler {
public:
    virtual ~TimerHandler() = default;
    virtual void on_timer_event(TimerId id) = 0;
};

} // namespace ddcs::runtime
