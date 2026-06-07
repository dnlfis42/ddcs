#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/common/fd.hpp"
#include "ddcs/runtime/detail/timer_handler_table.hpp"
#include "ddcs/runtime/detail/timer_queue.hpp"
#include "ddcs/runtime/fd_handler.hpp"
#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"

#include <chrono>
#include <optional>

#include <cstdint>

namespace ddcs::runtime {

class Reactor;

// timerfd 1개로 논리 타이머 N개를 예약, 취소, dispatch한다.
// PERF: cancel은 handler slot만 무효화하고, stale queue entry는 top에 도달했을 때 제거한다.
class TimerScheduler final : public FdHandler {
public: // 특수 멤버 함수
    TimerScheduler(Reactor& reactor);
    TimerScheduler(Reactor& reactor, common::Clock& clock);
    ~TimerScheduler() override;

    TimerScheduler(TimerScheduler const&) = delete;
    TimerScheduler& operator=(TimerScheduler const&) = delete;
    TimerScheduler(TimerScheduler&&) noexcept = delete;
    TimerScheduler& operator=(TimerScheduler&&) noexcept = delete;

public: // 수명주기
    void start();
    void stop() noexcept;

public: // 타이머 예약
    [[nodiscard]]
    TimerId schedule(std::chrono::nanoseconds delay, TimerHandler* handler);
    void cancel(TimerId id);

public: // 테스트 지원
    // NOTE: 주입 Clock 테스트에서 fd readiness 없이 현재 시각 기준 due timer를 처리한다.
    void expire_due();

public: // FdHandler 구현
    void on_fd_event(std::uint32_t events) override;

private:
    [[nodiscard]]
    bool drain();
    void prune_cancelled();
    [[nodiscard]]
    std::optional<detail::TimerQueue::time_point> next_deadline();
    void arm();

private:
    Reactor& reactor_;
    common::SteadyClock default_clock_;
    common::Clock& clock_;
    common::Fd fd_{};
    detail::TimerQueue timers_;
    detail::TimerHandlerTable handlers_;
    bool registered_{false};
};

} // namespace ddcs::runtime
