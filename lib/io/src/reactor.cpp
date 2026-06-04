#include "ddcs/io/reactor.hpp"

#include "ddcs/common/throw_errno.hpp"
#include "ddcs/logger/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <optional>

#include <cerrno>
#include <cstddef>
#include <cstdint>

#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace ddcs::io {

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

Reactor::Reactor() : clock_{default_clock_} { setup(); }

Reactor::Reactor(common::Clock& clock) : clock_{clock} { setup(); }

void Reactor::setup() {
    signal_pump_.r = this;

    epoll_fd_.reset(::epoll_create1(EPOLL_CLOEXEC));
    if (!epoll_fd_) {
        common::throw_errno(errno, "epoll_create1");
    }

    // SIGINT/SIGTERM 을 signalfd 로 - 루프 안에서 동기 처리(graceful stop).
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    if (::sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        common::throw_errno(errno, "sigprocmask");
    }
    signal_fd_.reset(::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
    if (!signal_fd_) {
        common::throw_errno(errno, "signalfd");
    }
    // signalfd 도 fd 디스패치 일원화: table 등록 후 토큰을 epoll data 에 싣는다.
    epoll_event sev{};
    sev.events = EPOLLIN | EPOLLET;
    sev.data.u64 = fd_dispatch_.insert(signal_fd_.get(), &signal_pump_);
    if (::epoll_ctl(epoll_fd_.get(), EPOLL_CTL_ADD, signal_fd_.get(), &sev) < 0) {
        common::throw_errno(errno, "epoll_ctl ADD signal_fd");
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
    fd_dispatch_.erase(fd);                                  // gen++ -> 진행 중 배치의 stale 토큰 무효화
}

TimerId Reactor::schedule(std::chrono::nanoseconds delay, TimerHandler* handler) {
    return timers_.schedule(clock_.now() + delay, handler);
}

void Reactor::cancel(TimerId id) noexcept { timers_.cancel(id); }

void Reactor::drain_signal() {
    signalfd_siginfo si{};
    while (::read(signal_fd_.get(), &si, sizeof(si)) > 0) {
        // ET 드레인. 어떤 시그널이든 hook 트리거.
    }
    if (signal_cb_) {
        signal_cb_();
    } else {
        stop(); // default: graceful stop
    }
}

void Reactor::SignalPump::on_io(std::uint32_t /*events*/) { r->drain_signal(); }

void Reactor::run() {
    running_ = true;
    LOG_INFO("io.reactor.start");
    while (running_) {
        run_once(std::chrono::milliseconds{-1}); // 무기한 블록
    }
}

void Reactor::run_once(std::chrono::milliseconds timeout) {
    std::array<epoll_event, max_epoll_events> events{};
    auto const n = wait(timeout, events.data(), events.size());
    if (!n) {
        return; // EINTR 등: 기존 동작대로 timer expire 없이 다음 호출에서 재시도
    }

    dispatch(events.data(), *n);
    timers_.expire(clock_.now()); // fd 이벤트 처리 뒤 due 타이머 fire
}

std::optional<int> Reactor::wait(std::chrono::milliseconds timeout, epoll_event* events, std::size_t capacity) {
    using std::chrono::milliseconds;

    auto const now = clock_.now();
    int wait_ms{};
    if (auto const dl = timers_.next_deadline()) {                                   // 모든 consumer 타이머 집계
        auto const remaining = std::max(*dl - now, common::Clock::duration::zero()); // 음수->0
        auto const due = std::chrono::ceil<milliseconds>(remaining);                 // 일찍 안 깸
        wait_ms = to_epoll_timeout(timeout.count() < 0 ? due : std::min(timeout, due));
    } else {
        wait_ms = to_epoll_timeout(timeout); // 타이머 없음(-1 가능)
    }

    int const n = ::epoll_wait(epoll_fd_.get(), events, static_cast<int>(capacity), wait_ms);
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
    LOG_INFO("io.reactor.stop");
}

} // namespace ddcs::io
