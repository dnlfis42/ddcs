#include "ddcs/io/timer_scheduler.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/detail/timer_queue.hpp"
#include "ddcs/io/detail/timer_registry.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/throw_errno.hpp"
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
    // it_value가 0이면 timerfd가 disarm되므로 즉시 만료도 최소 1 ns로 설정한다.
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

class TimerScheduler::Impl final : ChannelHandler {
public:
    explicit Impl(Reactor& reactor)
        : reactor_(reactor),
          clock_(default_clock_) {}

    Impl(Reactor& reactor, common::Clock& injected_clock)
        : reactor_(reactor),
          clock_(injected_clock) {}

    void on_ready(Channel& event_channel, ChannelEvents events) override {
        if (&event_channel != &channel_ || !contains(events, ChannelEvents::readable)) {
            return;
        }

        if (drain_timerfd()) {
            dispatch_expired();
        }
    }

    void start() {
        if (channel_.valid()) {
            return;
        }

        Fd fd{::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)};
        if (!fd.valid()) {
            throw_errno(errno, "timerfd_create");
        }

        channel_.init(std::move(fd), ChannelEvents::readable | ChannelEvents::edge_triggered, *this);

        if (auto const result = reactor_.add(channel_); !result) {
            channel_.close();
            throw_errno(result.err, "reactor add timer channel failed");
        }

        try {
            update_timerfd();
        } catch (...) {
            stop();
            throw;
        }
    }

    void stop() noexcept {
        if (channel_.registered()) {
            reactor_.remove(channel_);
        }
        if (channel_.valid()) {
            channel_.close();
        }
    }

    [[nodiscard]] bool active() const noexcept {
        return channel_.registered();
    }

    [[nodiscard]] TimerToken schedule(std::chrono::nanoseconds delay, TimerHandler& handler) {
        auto const deadline = clock_.now() + delay;
        auto const previous_deadline = next_deadline();

        TimerToken const id = timer_registrations_.insert(handler);
        timer_queue_.push(deadline, id);

        if (!previous_deadline || deadline < *previous_deadline) {
            update_timerfd();
        }
        return id;
    }

    void cancel(TimerToken id) {
        prune_cancelled();

        auto const next_timer = timer_queue_.top();
        bool const was_next_timer = next_timer && next_timer->id == id;

        if (!timer_registrations_.erase(id)) {
            return;
        }

        if (was_next_timer) {
            update_timerfd();
        }
    }

    void dispatch_expired() {
        auto const now = clock_.now();

        for (;;) {
            prune_cancelled();

            auto const timer = timer_queue_.top();
            if (!timer || timer->deadline > now) {
                break;
            }

            timer_queue_.pop();
            TimerHandler* handler = timer_registrations_.consume(timer->id);
            if (handler != nullptr) {
                handler->on_expired(timer->id);
            }
            // 콜백이 stop을 호출하면 channel이 해제되므로 즉시 멈춘다.
            if (!channel_.registered()) {
                return;
            }
        }

        update_timerfd();
    }

private:
    [[nodiscard]] std::optional<detail::TimerQueue::time_point> next_deadline() noexcept {
        prune_cancelled();

        auto const next_timer = timer_queue_.top();
        if (!next_timer) {
            return std::nullopt;
        }
        return next_timer->deadline;
    }

    // PERF: cancel은 등록 slot만 무효화하고, stale해진 heap entry는 top에 올라왔을 때 제거한다.
    void prune_cancelled() noexcept {
        while (auto const next_timer = timer_queue_.top()) {
            if (timer_registrations_.contains(next_timer->id)) {
                return;
            }
            timer_queue_.pop();
        }
    }

    [[nodiscard]] bool drain_timerfd() {
        bool has_expiration{false};

        for (;;) {
            std::uint64_t expirations{};
            ssize_t const n = ::read(channel_.fd(), &expirations, sizeof(expirations));
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
                throw_errno(err, "read timerfd");
            }
            return has_expiration;
        }
    }

    void update_timerfd() {
        if (!channel_.registered()) {
            return;
        }

        itimerspec spec{};
        if (auto const deadline = next_deadline()) {
            auto const remaining =
                std::max(*deadline - clock_.now(), common::Clock::duration::zero());
            spec =
                make_timerfd_spec(std::chrono::duration_cast<std::chrono::nanoseconds>(remaining));
        }

        if (::timerfd_settime(channel_.fd(), 0, &spec, nullptr) < 0) {
            throw_errno(errno, "timerfd_settime");
        }
    }

    Reactor& reactor_;
    common::SteadyClock default_clock_;
    common::Clock& clock_;
    Channel channel_;
    detail::TimerQueue timer_queue_;
    detail::TimerRegistry timer_registrations_;
};

TimerScheduler::TimerScheduler(Reactor& reactor)
    : impl_(std::make_unique<Impl>(reactor)) {}

TimerScheduler::TimerScheduler(Reactor& reactor, common::Clock& clock)
    : impl_(std::make_unique<Impl>(reactor, clock)) {}

TimerScheduler::~TimerScheduler() {
    stop();
}

void TimerScheduler::start() {
    impl_->start();
}

void TimerScheduler::stop() noexcept {
    impl_->stop();
}

bool TimerScheduler::active() const noexcept {
    return impl_->active();
}

TimerToken TimerScheduler::schedule(std::chrono::nanoseconds delay, TimerHandler& handler) {
    return impl_->schedule(delay, handler);
}

void TimerScheduler::cancel(TimerToken id) {
    impl_->cancel(id);
}

void TimerScheduler::dispatch_expired() {
    impl_->dispatch_expired();
}

} // namespace ddcs::io
