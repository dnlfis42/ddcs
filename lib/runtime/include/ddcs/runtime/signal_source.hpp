#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/runtime/fd_handler.hpp"

#include <functional>
#include <initializer_list>
#include <vector>

#include <cstdint>
#include <signal.h>

namespace ddcs::runtime {

class Reactor;

class SignalSource final : public FdHandler {
public:
    using Callback = std::function<void()>;

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

public: // FdHandler
    void on_io(std::uint32_t events) override;

private:
    [[nodiscard]]
    sigset_t make_mask() const;
    [[nodiscard]]
    bool drain();
    void restore_signal_mask() noexcept;

private:
    Reactor& reactor_;
    std::vector<int> signals_;
    Callback callback_;
    common::Fd fd_{};
    sigset_t previous_mask_{};
    bool has_previous_mask_{false};
    bool registered_{false};
};

} // namespace ddcs::runtime
