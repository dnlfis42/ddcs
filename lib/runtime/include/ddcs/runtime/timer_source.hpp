#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/common/fd.hpp"
#include "ddcs/runtime/fd_handler.hpp"
#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"
#include "ddcs/runtime/timer_queue.hpp"

#include <chrono>

#include <cstdint>

namespace ddcs::runtime {

class Reactor;

class TimerSource final : public FdHandler {
public:
    TimerSource(Reactor& reactor);
    TimerSource(Reactor& reactor, common::Clock& clock);
    ~TimerSource() override;

    TimerSource(TimerSource const&) = delete;
    TimerSource& operator=(TimerSource const&) = delete;
    TimerSource(TimerSource&&) noexcept = delete;
    TimerSource& operator=(TimerSource&&) noexcept = delete;

public:
    void start();
    void stop() noexcept;

public:
    [[nodiscard]]
    TimerId schedule(std::chrono::nanoseconds delay, TimerHandler* handler);
    void cancel(TimerId id);

public: // 테스트/주입 Clock용: fd readiness 없이 현재 시각 기준 due timer를 처리한다.
    void expire_due();

public: // FdHandler
    void on_io(std::uint32_t events) override;

private:
    [[nodiscard]]
    bool drain();
    void arm();

    Reactor& reactor_;
    common::SystemClock default_clock_;
    common::Clock& clock_;
    common::Fd fd_{};
    TimerQueue timers_;
    bool registered_{false};
};

} // namespace ddcs::runtime
