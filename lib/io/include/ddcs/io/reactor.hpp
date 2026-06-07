#pragma once

#include "ddcs/io/channel_events.hpp"

#include <chrono>
#include <memory>

namespace ddcs::io {

class Channel;

class Reactor {
public:
    Reactor();
    ~Reactor();

    Reactor(Reactor const&) = delete;
    Reactor& operator=(Reactor const&) = delete;
    Reactor(Reactor&&) noexcept = delete;
    Reactor& operator=(Reactor&&) noexcept = delete;

public:
    [[nodiscard]] bool add(Channel& channel);
    [[nodiscard]] bool modify(Channel& channel, ChannelEvents interests);
    void remove(Channel& channel) noexcept;

public:
    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
