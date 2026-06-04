#include "ddcs/runtime/signal_source.hpp"

#include "ddcs/common/throw_errno.hpp"
#include "ddcs/runtime/reactor.hpp"

#include <cerrno>
#include <cstdint>
#include <utility>

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace ddcs::runtime {

SignalSource::SignalSource(Reactor& reactor, std::initializer_list<int> signals, Callback callback)
    : reactor_{reactor}, signals_{signals}, callback_{std::move(callback)} {}

SignalSource::~SignalSource() { stop(); }

void SignalSource::start() {
    if (registered_) {
        return;
    }

    auto const mask = make_mask();
    if (::sigprocmask(SIG_BLOCK, &mask, &previous_mask_) < 0) {
        common::throw_errno(errno, "sigprocmask");
    }
    has_previous_mask_ = true;

    fd_.reset(::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
    if (!fd_) {
        int const err = errno;
        restore_signal_mask();
        common::throw_errno(err, "signalfd");
    }

    if (!reactor_.add(fd_.get(), EPOLLIN | EPOLLET, this)) {
        int const err = errno;
        fd_.reset();
        restore_signal_mask();
        common::throw_errno(err, "epoll_ctl ADD signal_fd");
    }
    registered_ = true;
}

void SignalSource::stop() noexcept {
    if (registered_) {
        reactor_.del(fd_.get());
        registered_ = false;
    }
    fd_.reset();
    restore_signal_mask();
}

void SignalSource::on_io(std::uint32_t events) {
    if ((events & EPOLLIN) == 0u) {
        return;
    }
    if (drain() && callback_) {
        callback_();
    }
}

sigset_t SignalSource::make_mask() const {
    sigset_t mask;
    sigemptyset(&mask);
    for (int const signal : signals_) {
        sigaddset(&mask, signal);
    }
    return mask;
}

bool SignalSource::drain() {
    bool delivered{false};
    for (;;) {
        signalfd_siginfo si{};
        ssize_t const n = ::read(fd_.get(), &si, sizeof(si));
        if (n == static_cast<ssize_t>(sizeof(si))) {
            delivered = true;
            continue;
        }
        if (n < 0) {
            int const err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return delivered;
            }
            if (err == EINTR) {
                continue;
            }
            common::throw_errno(err, "read signalfd");
        }
        return delivered;
    }
}

void SignalSource::restore_signal_mask() noexcept {
    if (!has_previous_mask_) {
        return;
    }
    (void)::sigprocmask(SIG_SETMASK, &previous_mask_, nullptr);
    has_previous_mask_ = false;
}

} // namespace ddcs::runtime
