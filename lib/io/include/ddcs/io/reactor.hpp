#pragma once

#include "ddcs/io/channel_events.hpp"

#include <chrono>
#include <memory>

namespace ddcs::io {

class Channel;

// epoll 기반 readiness 이벤트 루프
//   등록된 Channel보다 오래 살아야 한다.
//   스레드 안전하지 않으므로 단일 스레드 reactor loop에서만 호출한다.
class Reactor {
public:
    Reactor();
    ~Reactor();

    Reactor(Reactor const&) = delete;
    Reactor& operator=(Reactor const&) = delete;
    Reactor(Reactor&&) noexcept = delete;
    Reactor& operator=(Reactor&&) noexcept = delete;

    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop() noexcept;

    [[nodiscard]] bool running() const noexcept;

    // 이미 등록된 channel이면 그대로 두고 true
    [[nodiscard]] bool add(Channel& channel);
    // 등록되지 않은 channel이면 false
    [[nodiscard]] bool modify(Channel& channel, ChannelEvents interests);
    // 등록되지 않은 channel이면 무시한다.
    void remove(Channel& channel) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
