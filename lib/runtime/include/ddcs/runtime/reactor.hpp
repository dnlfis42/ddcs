#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/runtime/fd_dispatch_table.hpp"
#include "ddcs/runtime/fd_handler.hpp"

#include <chrono>
#include <optional>

#include <cstddef>
#include <cstdint>

struct epoll_event;

namespace ddcs::runtime {

// Edge-Triggered epoll 기반, single-thread fd readiness reactor.
//
// 핵심 책임: fd add/mod/del, epoll_wait, FdHandler dispatch, loop stop.
// 모르는 것: timer / signal / Connection / frame / FSM / SessionId / app 개념.
//
// fd 디스패치는 FdDispatchTable(gen-token)로 일원화 - epoll data.u64 에 토큰을 싣고 resolve 로 되찾는다.
// 같은 배치 안에서 닫힌 fd 의 stale 이벤트는 gen 불일치로 걸러져 use-after-free 가 없다(손님이 자기
// fd 를 그 자리에서 닫아도 안전).
class Reactor {
public: // special functions
    Reactor();
    ~Reactor();

    Reactor(Reactor const&) = delete;
    Reactor& operator=(Reactor const&) = delete;
    Reactor(Reactor&&) noexcept = delete;
    Reactor& operator=(Reactor&&) noexcept = delete;

public: // fd registry - 기계만. 실패는 bool 로 *보고*하고 의미부여는 호출자(핸들러) 몫.
    [[nodiscard]]
    bool add(int fd, std::uint32_t interest, FdHandler* handler);
    // interest 만 갱신(EPOLLOUT 토글 등). 핸들러는 add~del 동안 고정이라 토큰은 fd 로 복원한다.
    [[nodiscard]]
    bool mod(int fd, std::uint32_t interest);
    void del(int fd);

public: // loop
    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop() noexcept; // 멱등
    bool running() const noexcept { return running_; }

private:
    [[nodiscard]]
    std::optional<int> wait(std::chrono::milliseconds timeout, epoll_event* events, std::size_t capacity);
    void dispatch(epoll_event const* events, int count);

private:
    common::Fd epoll_fd_{};
    bool running_{false};

    FdDispatchTable fd_dispatch_; // fd -> FdHandler* (+gen). epoll data.u64 토큰의 출처/해석
};

} // namespace ddcs::runtime
