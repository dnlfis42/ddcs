#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/common/fd.hpp"
#include "ddcs/runtime/fd_dispatch_table.hpp"
#include "ddcs/runtime/fd_handler.hpp"
#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"
#include "ddcs/runtime/timer_queue.hpp"

#include <chrono>
#include <optional>

#include <cstddef>
#include <cstdint>

struct epoll_event;

namespace ddcs::runtime {

// Edge-Triggered epoll 기반, single-thread fd readiness reactor.
//
// 핵심 책임: fd add/mod/del, epoll_wait, FdHandler dispatch, loop stop.
// timer 는 아직 분리 전 호환 서비스로 이 클래스 안에 남아 있다.
// 모르는 것: Connection / frame / FSM / SessionId / 그 무엇이든 transport/app 개념.
//
// fd 디스패치는 FdDispatchTable(gen-token)로 일원화 - epoll data.u64 에 토큰을 싣고 resolve 로 되찾는다.
// 같은 배치 안에서 닫힌 fd 의 stale 이벤트는 gen 불일치로 걸러져 use-after-free 가 없다(손님이 자기
// fd 를 그 자리에서 닫아도 안전). 타이머는 TimerQueue(heap)에 모여 epoll_wait 타임아웃 하나로
// 구동된다(무장 syscall 0).
class Reactor {
public:                                     // special functions
    Reactor();                              // 내부 SystemClock 사용 (프로덕션 기본)
    explicit Reactor(common::Clock& clock); // Clock 주입 (테스트 결정성)
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

public: // transitional timer service - opaque TimerId. 취소된 타이머는 핸들러에게 안 온다.
    // delay 는 *상대* 지연(내부에서 clock_.now() + delay 로 절대 마감 계산).
    [[nodiscard]]
    TimerId schedule(std::chrono::nanoseconds delay, TimerHandler* handler);
    void cancel(TimerId id) noexcept; // O(1). heap 은 안 건드림(lazy). 멱등.

public: // loop
    void run();
    // 음수 = 무한블록. timer 분리 전까지 가장 이른 timer deadline 이 wait 상한을 낮춘다.
    void run_once(std::chrono::milliseconds timeout);
    void stop() noexcept; // 멱등
    bool running() const noexcept { return running_; }

private:
    void setup(); // 두 ctor 공통 1회 기동: epoll 생성
    [[nodiscard]]
    std::optional<int> wait(std::chrono::milliseconds timeout, epoll_event* events, std::size_t capacity);
    void dispatch(epoll_event const* events, int count);

    common::SystemClock default_clock_; // Reactor() 일 때의 기본 시계
    common::Clock& clock_;              // 타이머 기준 시계(주입 가능). default_clock_ 보다 뒤에 선언

    common::Fd epoll_fd_{};
    bool running_{false};

    FdDispatchTable fd_dispatch_; // fd -> FdHandler* (+gen). epoll data.u64 토큰의 출처/해석
    TimerQueue timers_;           // 전 소비자 타이머 집계 (heap, lazy-cancel)
};

} // namespace ddcs::runtime
