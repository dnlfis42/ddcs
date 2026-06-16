#include "ddcs/io/timer_scheduler.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/fd.hpp"
#include "ddcs/common/throw_errno.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/detail/timer_queue.hpp"
#include "ddcs/io/detail/timer_registration_table.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_handler.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include <sys/timerfd.h>
#include <unistd.h>

namespace ddcs::io {

namespace {

using namespace std::chrono_literals;

[[nodiscard]]
itimerspec make_timerfd_spec(std::chrono::nanoseconds delay) noexcept {
    if (delay <= 0ns) {
        delay = 1ns;
    }

    auto const max_seconds =
        static_cast<std::chrono::seconds::rep>(std::numeric_limits<time_t>::max());
    auto const seconds = std::chrono::duration_cast<std::chrono::seconds>(delay);
    if (seconds.count() >= max_seconds) {
        itimerspec spec{};
        spec.it_value.tv_sec = std::numeric_limits<time_t>::max();
        return spec;
    }

    auto const nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(delay - seconds).count();

    itimerspec spec{};
    spec.it_value.tv_sec = static_cast<time_t>(seconds.count());
    spec.it_value.tv_nsec = static_cast<long>(nsec);
    return spec;
}

} // namespace

struct TimerScheduler::Impl final : ChannelHandler {
    enum class State {
        ready,
        active,
    };

    explicit Impl(Reactor& reactor_ref)
        : reactor{reactor_ref},
          clock{default_clock} {}

    Impl(Reactor& reactor_ref, common::Clock& injected_clock)
        : reactor{reactor_ref},
          clock{injected_clock} {}

    [[nodiscard]] bool active() const noexcept {
        return state == State::active;
    }

    [[nodiscard]] TimerId schedule(std::chrono::nanoseconds delay, TimerHandler& handler) {
        auto const deadline = clock.now() + delay;
        auto const previous_deadline = next_deadline();

        TimerId const id = timer_registrations.insert(handler);
        timer_queue.push(deadline, id);

        if (!previous_deadline || deadline < *previous_deadline) {
            update_timerfd();
        }
        return id;
    }

    void cancel(TimerId id) {
        prune_cancelled();

        auto const next_timer = timer_queue.top();
        bool const was_next_timer = next_timer && next_timer->id == id;

        if (!timer_registrations.erase(id)) {
            return;
        }

        if (was_next_timer) {
            update_timerfd();
        }
    }

    void start() {
        if (state == State::active) {
            return;
        }

        common::Fd fd{::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)};
        if (!fd.valid()) {
            common::throw_errno(errno, "timerfd_create");
        }

        if (!channel.init(
                std::move(fd), ChannelEvents::readable | ChannelEvents::edge_triggered, *this
            )) {
            common::throw_errno(EINVAL, "timer channel init");
        }

        if (!reactor.add(channel)) {
            channel.reset();
            common::throw_errno(EINVAL, "reactor add timer channel");
        }

        try {
            update_timerfd();
            state = State::active;
        } catch (...) {
            stop_timerfd();
            throw;
        }
    }

    void stop() noexcept {
        if (state != State::active) {
            return;
        }

        stop_timerfd();
        state = State::ready;
    }

    void dispatch_expired() {
        auto const now = clock.now();

        for (;;) {
            prune_cancelled();

            auto const timer = timer_queue.top();
            if (!timer || timer->deadline > now) {
                break;
            }

            timer_queue.pop();
            TimerHandler* handler = timer_registrations.consume(timer->id);
            if (handler != nullptr) {
                handler->on_expired(timer->id);
            }
            if (!channel.registered()) {
                return;
            }
        }

        update_timerfd();
    }

    void on_ready(Channel& event_channel, ChannelEvents events) override {
        if (&event_channel != &channel || !contains(events, ChannelEvents::readable)) {
            return;
        }
        if (drain_timerfd()) {
            dispatch_expired();
        }
    }

    [[nodiscard]] std::optional<detail::TimerQueue::time_point> next_deadline() {
        prune_cancelled();

        auto const next_timer = timer_queue.top();
        if (!next_timer) {
            return std::nullopt;
        }
        return next_timer->deadline;
    }

    void prune_cancelled() {
        while (auto const next_timer = timer_queue.top()) {
            if (timer_registrations.contains(next_timer->id)) {
                return;
            }
            timer_queue.pop();
        }
    }

    [[nodiscard]] bool drain_timerfd() {
        bool has_expiration{false};

        for (;;) {
            std::uint64_t expirations{};
            ssize_t const n = ::read(channel.fd(), &expirations, sizeof(expirations));
            if (n == static_cast<ssize_t>(sizeof(expirations))) {
                has_expiration = true;
                continue;
            }
            if (n < 0) {
                int const err = errno;
                if (err == EAGAIN || err == EWOULDBLOCK) {
                    return has_expiration;
                }
                if (err == EINTR) {
                    continue;
                }
                common::throw_errno(err, "read timerfd");
            }
            return has_expiration;
        }
    }

    void update_timerfd() {
        if (!channel.registered()) {
            return;
        }

        itimerspec spec{};
        if (auto const deadline = next_deadline()) {
            auto const remaining =
                std::max(*deadline - clock.now(), common::Clock::duration::zero());
            spec =
                make_timerfd_spec(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining));
        }

        if (::timerfd_settime(channel.fd(), 0, &spec, nullptr) < 0) {
            common::throw_errno(errno, "timerfd_settime");
        }
    }

    void stop_timerfd() noexcept {
        if (channel.registered()) {
            reactor.remove(channel);
        }
        if (channel.valid()) {
            channel.reset();
        }
    }

    Reactor& reactor;
    common::SteadyClock default_clock;
    common::Clock& clock;
    State state{State::ready};
    Channel channel;
    detail::TimerQueue timer_queue;
    detail::TimerRegistrationTable timer_registrations;
};

TimerScheduler::TimerScheduler(Reactor& reactor)
    : impl_{std::make_unique<Impl>(reactor)} {}

TimerScheduler::TimerScheduler(Reactor& reactor, common::Clock& clock)
    : impl_{std::make_unique<Impl>(reactor, clock)} {}

TimerScheduler::~TimerScheduler() {
    stop();
}

bool TimerScheduler::active() const noexcept {
    return impl_->active();
}

TimerId TimerScheduler::schedule(std::chrono::nanoseconds delay, TimerHandler& handler) {
    return impl_->schedule(delay, handler);
}

void TimerScheduler::cancel(TimerId id) {
    impl_->cancel(id);
}

void TimerScheduler::start() {
    impl_->start();
}

void TimerScheduler::stop() noexcept {
    impl_->stop();
}

void TimerScheduler::dispatch_expired() {
    impl_->dispatch_expired();
}

} // namespace ddcs::io
