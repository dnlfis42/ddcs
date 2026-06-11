#pragma once

#include "ddcs/io/timer_id.hpp"

#include <chrono>
#include <memory>

namespace ddcs::common {

class Clock;

} // namespace ddcs::common

namespace ddcs::io {

class Reactor;
class TimerHandler;

class TimerScheduler {
public:
    TimerScheduler(Reactor& reactor);
    TimerScheduler(Reactor& reactor, common::Clock& clock);
    ~TimerScheduler();

    TimerScheduler(TimerScheduler const&) = delete;
    TimerScheduler& operator=(TimerScheduler const&) = delete;
    TimerScheduler(TimerScheduler&&) noexcept = delete;
    TimerScheduler& operator=(TimerScheduler&&) noexcept = delete;

    [[nodiscard]] bool active() const noexcept;

    [[nodiscard]] TimerId schedule(std::chrono::nanoseconds delay, TimerHandler& handler);
    void cancel(TimerId id);

    void start();
    void stop() noexcept;

    // 테스트 지원
    void dispatch_expired();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
