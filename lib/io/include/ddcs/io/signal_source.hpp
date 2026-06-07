#pragma once

#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"

#include <functional>
#include <initializer_list>
#include <memory>

namespace ddcs::io {

class Channel;
class Reactor;

class SignalSource final : private ChannelHandler {
public:
    using Callback = std::function<void(int signal)>;

public:
    SignalSource(Reactor& reactor, std::initializer_list<int> signals, Callback callback);
    ~SignalSource() override;

    SignalSource(SignalSource const&) = delete;
    SignalSource& operator=(SignalSource const&) = delete;
    SignalSource(SignalSource&&) noexcept = delete;
    SignalSource& operator=(SignalSource&&) noexcept = delete;

public:
    void start();
    void stop() noexcept;
    bool running() const noexcept;

private: // ddcs::io::ChannelHandler
    void on_ready(Channel& channel, ChannelEvents events) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
