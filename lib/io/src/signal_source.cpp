#include "ddcs/io/signal_source.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/common/throw_errno.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/reactor.hpp"

#include <cerrno>
#include <csignal>
#include <utility>
#include <vector>

#include <pthread.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace ddcs::io {

struct SignalSource::Impl final : ChannelHandler {
    enum class State {
        ready,
        active,
    };

    Impl(Reactor& reactor_ref, std::initializer_list<int> signal_list, Callback callback_fn)
        : reactor{reactor_ref},
          signals{signal_list},
          callback{std::move(callback_fn)} {}

    [[nodiscard]] bool active() const noexcept {
        return state == State::active;
    }

    void start() {
        if (state == State::active) {
            return;
        }

        sigset_t signal_mask;
        sigemptyset(&signal_mask);
        for (int const signal : signals) {
            sigaddset(&signal_mask, signal);
        }

        if (int const err = ::pthread_sigmask(SIG_BLOCK, &signal_mask, &previous_signal_mask);
            err != 0) {
            common::throw_errno(err, "pthread_sigmask");
        }
        has_previous_signal_mask = true;

        common::Fd fd{::signalfd(-1, &signal_mask, SFD_NONBLOCK | SFD_CLOEXEC)};
        if (!fd.valid()) {
            int const err = errno;
            restore_previous_signal_mask();
            common::throw_errno(err, "signalfd");
        }

        if (!channel.init(
                std::move(fd), ChannelEvents::readable | ChannelEvents::edge_triggered, *this
            )) {
            restore_previous_signal_mask();
            common::throw_errno(EINVAL, "signal channel init");
        }

        if (!reactor.add(channel)) {
            channel.reset();
            restore_previous_signal_mask();
            common::throw_errno(EINVAL, "reactor add signal channel");
        }

        state = State::active;
    }

    void stop() noexcept {
        if (state != State::active) {
            return;
        }

        if (channel.registered()) {
            reactor.remove(channel);
        }
        if (channel.valid()) {
            channel.reset();
        }

        restore_previous_signal_mask();
        state = State::ready;
    }

    void on_ready(Channel& event_channel, ChannelEvents events) override {
        if (&event_channel != &channel || !contains(events, ChannelEvents::readable)) {
            return;
        }
        drain_signalfd();
    }

    void drain_signalfd() {
        for (;;) {
            signalfd_siginfo info{};
            ssize_t const n = ::read(channel.fd(), &info, sizeof(info));
            if (n == static_cast<ssize_t>(sizeof(info))) {
                if (callback) {
                    callback(static_cast<int>(info.ssi_signo));
                }
                if (!channel.registered()) {
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
                common::throw_errno(err, "read signalfd");
            }
            return;
        }
    }

    void restore_previous_signal_mask() noexcept {
        if (!has_previous_signal_mask) {
            return;
        }

        if (::pthread_sigmask(SIG_SETMASK, &previous_signal_mask, nullptr) == 0) {
            has_previous_signal_mask = false;
        }
    }

    Reactor& reactor;
    std::vector<int> signals;
    Callback callback;
    State state{State::ready};
    Channel channel;
    sigset_t previous_signal_mask{};
    bool has_previous_signal_mask{false};
};

SignalSource::SignalSource(Reactor& reactor, std::initializer_list<int> signals, Callback callback)
    : impl_{std::make_unique<Impl>(reactor, signals, std::move(callback))} {}

SignalSource::~SignalSource() {
    stop();
}

bool SignalSource::active() const noexcept {
    return impl_->active();
}

void SignalSource::start() {
    impl_->start();
}

void SignalSource::stop() noexcept {
    impl_->stop();
}

} // namespace ddcs::io
