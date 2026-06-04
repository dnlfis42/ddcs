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

// chrono::ms -> epoll_wait timeout(int). 음수 = 무한블록(-1), int 범위 초과는 INT_MAX 로 clamp.
[[nodiscard]]
int to_epoll_timeout(std::chrono::milliseconds t) noexcept {
    auto const ms = t.count(); // int64
    if (ms < 0) {
        return -1; // 무한블록
    }
    if (ms > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(ms);
}

} // namespace

Reactor::Reactor() {
    epoll_fd_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!epoll_fd_) {
        common::throw_errno(errno, "epoll_create1");
    }
}

Reactor::~Reactor() { stop(); }

bool Reactor::add(int fd, std::uint32_t interest, FdHandler* handler) {
    epoll_event ev{};
    ev.events = interest;
    ev.data.u64 = fd_dispatch_.insert(fd, handler);
    if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, fd, &ev) != 0) {
        fd_dispatch_.erase(fd); // 롤백: 테이블에 dangling 핸들러를 남기지 않음
        return false;
    }
    return true;
}

bool Reactor::mod(int fd, std::uint32_t interest) {
    epoll_event ev{};
    ev.events = interest;
    ev.data.u64 = fd_dispatch_.token(fd); // MOD 가 data 를 덮어쓰므로 토큰 재지정(핸들러 불변)
    return ::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_MOD, fd, &ev) == 0;
}

void Reactor::del(int fd) {
    ::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_DEL, fd, nullptr); // best-effort
    fd_dispatch_.erase(fd);                                   // gen++ -> 진행 중 배치의 stale 토큰 무효화
}

void Reactor::run() {
    running_ = true;
    LOG_INFO("runtime.reactor.start");
    while (running_) {
        run_once(std::chrono::milliseconds{-1}); // 무기한 블록
    }
}

void Reactor::run_once(std::chrono::milliseconds timeout) {
    std::array<epoll_event, max_epoll_events> events{};
    auto const n = wait(timeout, events.data(), events.size());
    if (!n) {
        return; // EINTR 등: 다음 호출에서 재시도
    }

    dispatch(events.data(), *n);
}

std::optional<int> Reactor::wait(std::chrono::milliseconds timeout, epoll_event* events, std::size_t capacity) {
    int const n = ::epoll_wait(epoll_fd_.get(), events, static_cast<int>(capacity), to_epoll_timeout(timeout));
    if (n < 0) {
        if (errno == EINTR) {
            return std::nullopt; // 깨움 - 다음 이터레이션 재시도
        }
        common::throw_errno(errno, "epoll_wait");
    }
    return n;
}

void Reactor::dispatch(epoll_event const* events, int count) {
    for (int i = 0; i < count; ++i) {
        auto const& e = events[static_cast<std::size_t>(i)];
        if (auto* h = fd_dispatch_.resolve(e.data.u64)) { // gen 불일치(닫힌 fd)면 skip - UAF 차단
            h->on_io(e.events);
        }
    }
}

void Reactor::stop() noexcept {
    if (!running_) {
        return; // 멱등
    }
    running_ = false;
    LOG_INFO("runtime.reactor.stop");
}

} // namespace ddcs::runtime
