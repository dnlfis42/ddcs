#pragma once

#include <functional>
#include <initializer_list>
#include <memory>

namespace ddcs::io {

class Channel;
class Reactor;

class SignalSource {
public:
    using Callback = std::function<void(int signal)>;

public:
    SignalSource(Reactor& reactor, std::initializer_list<int> signals, Callback callback);
    ~SignalSource();

    SignalSource(SignalSource const&) = delete;
    SignalSource& operator=(SignalSource const&) = delete;
    SignalSource(SignalSource&&) noexcept = delete;
    SignalSource& operator=(SignalSource&&) noexcept = delete;

    [[nodiscard]] bool active() const noexcept;

    void start();
    void stop() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
