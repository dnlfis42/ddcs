#pragma once

#include "ddcs/io/timer_id.hpp"

namespace ddcs::io {

// 만료된 timer를 받는 콜백 인터페이스
class TimerHandler {
public:
    virtual ~TimerHandler() = default;

    // 등록한 delay가 지나면 TimerScheduler가 id로 호출한다.
    virtual void on_expired(TimerId id) = 0;
};

} // namespace ddcs::io
