#include "ddcs/runtime/timer_source.hpp"

#include "ddcs/common/throw_errno.hpp"
#include "ddcs/runtime/reactor.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#include <cerrno>
#include <cstdint>
#include <ctime>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace ddcs::runtime {

namespace {

using namespace std::chrono_literals;

[[nodiscard]]
itimerspec make_spec(std::chrono::nanoseconds delay) noexcept {
    if (delay <= 0ns) {
        delay = 1ns;
    }

    auto const max_seconds = static_cast<std::chrono::seconds::rep>(std::numeric_limits<time_t>::max());
    auto const seconds_duration = std::chrono::duration_cast<std::chrono::seconds>(delay);
    if (seconds_duration.count() >= max_seconds) {
        itimerspec spec{};
        spec.it_value.tv_sec = std::numeric_limits<time_t>::max();
        return spec;
    }

    auto const nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(delay - seconds_duration).count();

    itimerspec spec{};
    spec.it_value.tv_sec = static_cast<time_t>(seconds_duration.count());
    spec.it_value.tv_nsec = static_cast<long>(nanos);
    return spec;
}

} // namespace

TimerSource::TimerSource(Reactor& reactor) : reactor_{reactor}, clock_{default_clock_} {}

TimerSource::TimerSource(Reactor& reactor, common::Clock& clock) : reactor_{reactor}, clock_{clock} {}

TimerSource::~TimerSource() { stop(); }

void TimerSource::start() {
    if (registered_) {
        return;
    }

    fd_.reset(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC));
    if (!fd_) {
        common::throw_errno(errno, "timerfd_create");
    }

    if (!reactor_.add(fd_.get(), EPOLLIN | EPOLLET, this)) {
        int const err = errno;
        fd_.reset();
        common::throw_errno(err, "epoll_ctl ADD timer_fd");
    }
    registered_ = true;

    try {
        arm();
    } catch (...) {
        stop();
        throw;
    }
}

void TimerSource::stop() noexcept {
    if (registered_) {
        reactor_.del(fd_.get());
        registered_ = false;
    }
    fd_.reset();
}

TimerId TimerSource::schedule(std::chrono::nanoseconds delay, TimerHandler* handler) {
    TimerId const id = timers_.schedule(clock_.now() + delay, handler);
    arm();
    return id;
}

void TimerSource::cancel(TimerId id) {
    timers_.cancel(id);
    arm();
}

void TimerSource::expire_due() {
    timers_.expire(clock_.now());
    arm();
}

void TimerSource::on_io(std::uint32_t events) {
    if ((events & EPOLLIN) == 0u) {
        return;
    }
    if (drain()) {
        expire_due();
    }
}

bool TimerSource::drain() {
    bool delivered{false};
    for (;;) {
        std::uint64_t expirations{};
        ssize_t const n = ::read(fd_.get(), &expirations, sizeof(expirations));
        if (n == static_cast<ssize_t>(sizeof(expirations))) {
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
            common::throw_errno(err, "read timerfd");
        }
        return delivered;
    }
}

void TimerSource::arm() {
    if (!registered_) {
        return;
    }

    itimerspec spec{};
    if (auto const deadline = timers_.next_deadline()) {
        auto const remaining = std::max(*deadline - clock_.now(), common::Clock::duration::zero());
        spec = make_spec(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining));
    }

    if (::timerfd_settime(fd_.get(), 0, &spec, nullptr) < 0) {
        common::throw_errno(errno, "timerfd_settime");
    }
}

} // namespace ddcs::runtime
