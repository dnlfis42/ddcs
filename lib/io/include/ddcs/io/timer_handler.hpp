#pragma once

#include "ddcs/io/timer_id.hpp"

namespace ddcs::io {

/**
 * @brief 타이머 만료 시 호출되는 핸들러 인터페이스
 *
 */
class TimerHandler {
public:
    virtual ~TimerHandler() = default;
    virtual void on_timer(TimerId id) = 0;
};

} // namespace ddcs::io
