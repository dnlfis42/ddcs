#include "ddcs/runtime/timer_scheduler.hpp"

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

TimerScheduler::TimerScheduler(Reactor& reactor) : reactor_{reactor}, clock_{default_clock_} {}

TimerScheduler::TimerScheduler(Reactor& reactor, common::Clock& clock) : reactor_{reactor}, clock_{clock} {}

TimerScheduler::~TimerScheduler() { stop(); }

void TimerScheduler::start() {
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

void TimerScheduler::stop() noexcept {
    if (registered_) {
        reactor_.del(fd_.get());
        registered_ = false;
    }
    fd_.reset();
}

TimerId TimerScheduler::schedule(std::chrono::nanoseconds delay, TimerHandler* handler) {
    TimerId const id = handlers_.insert(handler);
    timers_.push(clock_.now() + delay, id);
    arm();
    return id;
}

void TimerScheduler::cancel(TimerId id) {
    (void)handlers_.erase(id);
    arm();
}

void TimerScheduler::expire_due() {
    auto const now = clock_.now();

    for (;;) {
        prune_cancelled();

        auto const entry = timers_.top();
        if (!entry || entry->deadline > now) {
            break;
        }

        timers_.pop();
        TimerHandler* handler = handlers_.consume(entry->id);
        if (handler != nullptr) {
            handler->on_timer_event(entry->id);
        }
    }

    arm();
}

void TimerScheduler::on_fd_event(std::uint32_t events) {
    if ((events & EPOLLIN) == 0u) {
        return;
    }
    if (drain()) {
        expire_due();
    }
}

bool TimerScheduler::drain() {
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

void TimerScheduler::prune_cancelled() {
    while (auto const entry = timers_.top()) {
        if (handlers_.contains(entry->id)) {
            return;
        }
        timers_.pop();
    }
}

std::optional<detail::TimerQueue::time_point> TimerScheduler::next_deadline() {
    prune_cancelled();
    auto const entry = timers_.top();
    if (!entry) {
        return std::nullopt;
    }
    return entry->deadline;
}

void TimerScheduler::arm() {
    if (!registered_) {
        return;
    }

    itimerspec spec{};
    if (auto const deadline = next_deadline()) {
        auto const remaining = std::max(*deadline - clock_.now(), common::Clock::duration::zero());
        spec = make_spec(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining));
    }

    if (::timerfd_settime(fd_.get(), 0, &spec, nullptr) < 0) {
        common::throw_errno(errno, "timerfd_settime");
    }
}

} // namespace ddcs::runtime
