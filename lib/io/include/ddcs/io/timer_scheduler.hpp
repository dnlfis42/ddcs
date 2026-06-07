#pragma once

#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <chrono>
#include <memory>

namespace ddcs::common {

class Clock;

} // namespace ddcs::common

namespace ddcs::io {

class Channel;
class Reactor;
class TimerHandler;

class TimerScheduler final : private ChannelHandler {
public:
    TimerScheduler(Reactor& reactor);
    TimerScheduler(Reactor& reactor, common::Clock& clock);
    ~TimerScheduler() override;

    TimerScheduler(TimerScheduler const&) = delete;
    TimerScheduler& operator=(TimerScheduler const&) = delete;
    TimerScheduler(TimerScheduler&&) noexcept = delete;
    TimerScheduler& operator=(TimerScheduler&&) noexcept = delete;

public:
    void start();
    void stop() noexcept;

public:
    [[nodiscard]] TimerId schedule(std::chrono::nanoseconds delay, TimerHandler& handler);
    void cancel(TimerId id);

public: // 테스트 지원
    void dispatch_expired();

private: // ChannelHandler
    void on_ready(Channel& channel, ChannelEvents events) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
