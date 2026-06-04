#pragma once

#include "ddcs/runtime/timer_id.hpp"

namespace ddcs::runtime {

/**
 * @brief 타이머 만료 시 호출되는 핸들러 인터페이스
 *
 */
class TimerHandler {
public:
    virtual ~TimerHandler() = default;
    virtual void on_timer(TimerId id) = 0;
};

} // namespace ddcs::runtime
