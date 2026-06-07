#pragma once

#include "ddcs/io/timer_id.hpp"

namespace ddcs::io {

class TimerHandler {
public:
    virtual ~TimerHandler() = default;
    virtual void on_expired(TimerId id) = 0;
};

} // namespace ddcs::io
