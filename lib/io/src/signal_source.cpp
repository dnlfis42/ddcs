#include "ddcs/io/signal_source.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/common/throw_errno.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/reactor.hpp"

#include <utility>
#include <vector>

#include <cerrno>
#include <csignal>

#include <sys/signalfd.h>
#include <unistd.h>

namespace ddcs::io {

struct SignalSource::Impl {
    Impl(SignalSource& owner_ref, Reactor& reactor_ref, std::initializer_list<int> signal_list, Callback callback_fn)
        : owner{owner_ref}, reactor{reactor_ref}, signals{signal_list}, callback{std::move(callback_fn)} {}

    void start() {
        if (channel.registered()) {
            return;
        }

        sigset_t signal_mask;
        sigemptyset(&signal_mask);
        for (int const signal : signals) {
            sigaddset(&signal_mask, signal);
        }

        if (::sigprocmask(SIG_BLOCK, &signal_mask, &previous_signal_mask) < 0) {
            common::throw_errno(errno, "sigprocmask");
        }
        has_previous_signal_mask = true;

        common::Fd fd{::signalfd(-1, &signal_mask, SFD_NONBLOCK | SFD_CLOEXEC)};
        if (!fd.valid()) {
            int const err = errno;
            restore_previous_signal_mask();
            common::throw_errno(err, "signalfd");
        }

        if (!channel.init(std::move(fd), ChannelEvents::readable | ChannelEvents::edge_triggered, owner)) {
            restore_previous_signal_mask();
            common::throw_errno(EINVAL, "signal channel init");
        }

        if (!reactor.add(channel)) {
            int const err = errno;
            channel.reset();
            restore_previous_signal_mask();
            common::throw_errno(err, "epoll_ctl ADD signalfd");
        }
    }

    void stop() noexcept {
        if (channel.registered()) {
            reactor.remove(channel);
        }
        channel.reset();
        restore_previous_signal_mask();
    }

    [[nodiscard]] bool running() const noexcept { return channel.registered(); }

    void on_ready(Channel& event_channel, ChannelEvents events) {
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

        if (::sigprocmask(SIG_SETMASK, &previous_signal_mask, nullptr) == 0) {
            has_previous_signal_mask = false;
        }
    }

    SignalSource& owner;
    Reactor& reactor;
    std::vector<int> signals;
    Callback callback;
    Channel channel;
    sigset_t previous_signal_mask{};
    bool has_previous_signal_mask{false};
};

SignalSource::SignalSource(Reactor& reactor, std::initializer_list<int> signals, Callback callback)
    : impl_{std::make_unique<Impl>(*this, reactor, signals, std::move(callback))} {}

SignalSource::~SignalSource() { stop(); }

void SignalSource::start() { impl_->start(); }

void SignalSource::stop() noexcept { impl_->stop(); }

bool SignalSource::running() const noexcept { return impl_->running(); }

void SignalSource::on_ready(Channel& channel, ChannelEvents events) { impl_->on_ready(channel, events); }

} // namespace ddcs::io
