#include "ddcs/runtime/reactor.hpp"

#include "ddcs/common/throw_errno.hpp"
#include "ddcs/logger/log.hpp"

#include <array>
#include <chrono>
#include <limits>
#include <optional>

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <sys/epoll.h>

namespace ddcs::runtime {

namespace {

constexpr int max_epoll_events{64};
constexpr int infinite_epoll_timeout{-1};
constexpr std::chrono::milliseconds wait_forever{-1};

[[nodiscard]]
int to_epoll_timeout(std::chrono::milliseconds timeout) noexcept {
    auto const milliseconds = timeout.count();
    if (milliseconds < 0) {
        return infinite_epoll_timeout;
    }
    if (milliseconds > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(milliseconds);
}

} // namespace

Reactor::Reactor() {
    epoll_fd_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!epoll_fd_.valid()) {
        common::throw_errno(errno, "epoll_create1");
    }
}

Reactor::~Reactor() { stop(); }

bool Reactor::add(int fd, std::uint32_t interest, FdHandler* handler) {
    epoll_event ev{};
    ev.events = interest;
    ev.data.u64 = fd_handlers_.insert(fd, handler);
    if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &ev) != 0) {
        fd_handlers_.erase(fd);
        return false;
    }
    return true;
}

bool Reactor::mod(int fd, std::uint32_t interest) {
    epoll_event ev{};
    ev.events = interest;
    ev.data.u64 = fd_handlers_.token(fd);
    return ::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &ev) == 0;
}

void Reactor::del(int fd) {
    (void)::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr);
    fd_handlers_.erase(fd);
}

void Reactor::run() {
    running_ = true;
    LOG_INFO("runtime.reactor.start");
    while (running_) {
        run_once(wait_forever);
    }
}

void Reactor::run_once(std::chrono::milliseconds timeout) {
    std::array<epoll_event, max_epoll_events> events{};
    auto const n = wait(timeout, events.data(), events.size());
    if (!n) {
        return;
    }

    dispatch(events.data(), *n);
}

std::optional<int> Reactor::wait(std::chrono::milliseconds timeout, epoll_event* events, std::size_t capacity) {
    int const n = ::epoll_wait(epoll_fd_.get(), events, static_cast<int>(capacity), to_epoll_timeout(timeout));
    if (n < 0) {
        if (errno == EINTR) {
            return std::nullopt;
        }
        common::throw_errno(errno, "epoll_wait");
    }
    return n;
}

void Reactor::dispatch(epoll_event const* events, int count) {
    for (int i = 0; i < count; ++i) {
        auto const& event = events[static_cast<std::size_t>(i)];
        if (auto* handler = fd_handlers_.resolve(event.data.u64)) {
            handler->on_fd_event(event.events);
        }
    }
}

void Reactor::stop() noexcept {
    if (!running_) {
        return;
    }
    running_ = false;
    LOG_INFO("runtime.reactor.stop");
}

} // namespace ddcs::runtime
