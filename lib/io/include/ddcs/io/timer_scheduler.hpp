#pragma once

#include "ddcs/io/timer_token.hpp"

#include <chrono>
#include <memory>

namespace ddcs::common {

class Clock;

} // namespace ddcs::common

namespace ddcs::io {

class Reactor;
class TimerHandler;

// timerfd 하나로 다수 TimerToken을 deadline 순서로 만료시킨다.
//
// Reactor보다 먼저 소멸해야 한다.
class TimerScheduler {
public:
    TimerScheduler(Reactor& reactor);
    // 테스트용 생성자
    //
    // 주입된 clock은 실제 timerfd와 분리되므로 dispatch_expired로 구동한다.
    TimerScheduler(Reactor& reactor, common::Clock& clock);
    ~TimerScheduler();

    TimerScheduler(TimerScheduler const&) = delete;
    TimerScheduler& operator=(TimerScheduler const&) = delete;
    TimerScheduler(TimerScheduler&&) noexcept = delete;
    TimerScheduler& operator=(TimerScheduler&&) noexcept = delete;

    void start();
    void stop() noexcept;

    [[nodiscard]] bool active() const noexcept;

    [[nodiscard]] TimerToken schedule(std::chrono::nanoseconds delay, TimerHandler& handler);
    void cancel(TimerToken id);

    // 테스트 지원
    void dispatch_expired();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
