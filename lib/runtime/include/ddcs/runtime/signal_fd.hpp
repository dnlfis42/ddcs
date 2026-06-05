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

// signalfd를 소유하고 Reactor의 fd event를 callback으로 변환한다.
// CAUTION: start()는 process signal mask를 변경하고, stop()은 이전 mask를 복구한다.
class SignalFd final : public FdHandler {
public:
    using Callback = std::function<void()>;

public: // 특수 멤버 함수
    SignalFd(Reactor& reactor, std::initializer_list<int> signals, Callback callback);
    ~SignalFd() override;

    SignalFd(SignalFd const&) = delete;
    SignalFd& operator=(SignalFd const&) = delete;
    SignalFd(SignalFd&&) noexcept = delete;
    SignalFd& operator=(SignalFd&&) noexcept = delete;

public: // 수명주기
    void start();
    void stop() noexcept;

public: // FdHandler 구현
    void on_fd_event(std::uint32_t events) override;

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
