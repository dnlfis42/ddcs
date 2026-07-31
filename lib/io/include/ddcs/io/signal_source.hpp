#pragma once

#include <functional>
#include <initializer_list>
#include <memory>

namespace ddcs::io {

class Channel;
class Reactor;

// signalfd로 받은 signal을 Reactor 콜백으로 전달하는 source
//
// Reactor보다 먼저 소멸해야 한다.
class SignalSource {
public:
    using Callback = std::function<void(int signal)>;

    SignalSource(Reactor& reactor, std::initializer_list<int> signals, Callback callback);
    ~SignalSource();

    SignalSource(SignalSource const&) = delete;
    SignalSource& operator=(SignalSource const&) = delete;
    SignalSource(SignalSource&&) noexcept = delete;
    SignalSource& operator=(SignalSource&&) noexcept = delete;

    // 호출 스레드에서 signal을 block한다.
    // 멀티스레드면 다른 스레드도 같은 signal을 block해야 한다.
    void start();
    void stop() noexcept;

    [[nodiscard]] bool active() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::io
