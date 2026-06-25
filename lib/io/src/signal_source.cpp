#include "ddcs/io/signal_source.hpp"

#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/throw_errno.hpp"

#include <cerrno>
#include <csignal>
#include <utility>
#include <vector>

#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace ddcs::io {

class SignalSource::Impl final : ChannelHandler {
public:
    enum class State {
        ready,
        active,
    };

    Impl(Reactor& reactor, std::initializer_list<int> signals, Callback callback)
        : reactor_(reactor),
          signals_(signals),
          callback_(std::move(callback)) {}

    void on_ready(Channel& event_channel, ChannelEvents events) override {
        if (&event_channel != &channel_ || !contains(events, ChannelEvents::readable)) {
            return;
        }

        drain_signalfd();
    }

    void start() {
        if (state_ == State::active) {
            return;
        }

        sigset_t signal_mask;
        sigemptyset(&signal_mask);
        for (int const signal : signals_) {
            sigaddset(&signal_mask, signal);
        }

        if (int const err = ::pthread_sigmask(SIG_BLOCK, &signal_mask, &previous_signal_mask_);
            err != 0) {
            io::throw_errno(err, "pthread_sigmask");
        }
        has_previous_signal_mask_ = true;

        io::Fd fd{::signalfd(-1, &signal_mask, SFD_NONBLOCK | SFD_CLOEXEC)};
        if (!fd.valid()) {
            int const err = errno;
            restore_previous_signal_mask();
            io::throw_errno(err, "signalfd");
        }

        if (!channel_.init(
                std::move(fd), ChannelEvents::readable | ChannelEvents::edge_triggered, *this
            )) {
            restore_previous_signal_mask();
            io::throw_errno(EINVAL, "signal channel init failed");
        }

        if (!reactor_.add(channel_)) {
            channel_.reset();
            restore_previous_signal_mask();
            io::throw_errno(EINVAL, "reactor add signal channel failed");
        }

        state_ = State::active;
    }

    void stop() noexcept {
        if (state_ != State::active) {
            return;
        }

        if (channel_.registered()) {
            reactor_.remove(channel_);
        }
        if (channel_.valid()) {
            channel_.reset();
        }

        restore_previous_signal_mask();
        state_ = State::ready;
    }

    [[nodiscard]] bool active() const noexcept {
        return state_ == State::active;
    }

private:
    void drain_signalfd() {
        for (;;) {
            signalfd_siginfo info{};
            ssize_t const n = ::read(channel_.fd(), &info, sizeof(info));
            if (n == static_cast<ssize_t>(sizeof(info))) {
                if (callback_) {
                    callback_(static_cast<int>(info.ssi_signo));
                }
                // CAUTION: 콜백이 stop을 호출하면 channel이 해제되므로 멈춘다
                if (!channel_.registered()) {
                    return;
                }
                continue;
            }
            if (n < 0) {
                int const err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK) {
                    return;
                }
                if (err == EINTR) {
                    continue;
                }
                io::throw_errno(err, "read signalfd");
            }
            return;
        }
    }

    void restore_previous_signal_mask() noexcept {
        if (!has_previous_signal_mask_) {
            return;
        }

        if (::pthread_sigmask(SIG_SETMASK, &previous_signal_mask_, nullptr) == 0) {
            has_previous_signal_mask_ = false;
        }
    }

    Reactor& reactor_;
    std::vector<int> signals_;
    Callback callback_;
    State state_ = State::ready;
    Channel channel_;
    sigset_t previous_signal_mask_{};
    bool has_previous_signal_mask_ = false;
};

SignalSource::SignalSource(Reactor& reactor, std::initializer_list<int> signals, Callback callback)
    : impl_(std::make_unique<Impl>(reactor, signals, std::move(callback))) {}

SignalSource::~SignalSource() {
    stop();
}

void SignalSource::start() {
    impl_->start();
}

void SignalSource::stop() noexcept {
    impl_->stop();
}

bool SignalSource::active() const noexcept {
    return impl_->active();
}

} // namespace ddcs::io
