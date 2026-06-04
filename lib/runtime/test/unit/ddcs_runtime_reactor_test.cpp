#include "ddcs/runtime/reactor.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/runtime/fd_handler.hpp"
#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <csignal>
#include <cstdint>

#include <sys/epoll.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

using ddcs::runtime::FdHandler;
using ddcs::runtime::Reactor;
using ddcs::runtime::TimerHandler;
using ddcs::runtime::TimerId;

class FlagTimer : public TimerHandler {
public:
    int count{0};
    TimerId last{};
    void on_timer(TimerId id) override {
        ++count;
        last = id;
    }
};

class FlagIo : public FdHandler {
public:
    int count{0};
    std::uint32_t last_events{0};
    void on_io(std::uint32_t events) override {
        ++count;
        last_events = events;
    }
};

} // namespace

TEST(ReactorTest, TimerFires) {
    Reactor r;
    FlagTimer h;
    TimerId const id = r.schedule(5ms, &h); // ms -> ns 무손실 widening
    r.run_once(1000ms);                     // 1s 안에 timerfd 발화 -> 핸들러 호출
    EXPECT_EQ(h.count, 1);
    EXPECT_EQ(h.last, id);
}

TEST(ReactorTest, CancelledTimerDoesNotFire) {
    Reactor r;
    FlagTimer h;
    TimerId const id = r.schedule(5ms, &h);
    r.cancel(id);
    r.run_once(50ms); // 마감(5ms) 지나도 - 취소분은 fire 안 됨
    EXPECT_EQ(h.count, 0);
}

TEST(ReactorTest, FdReadableDispatches) {
    Reactor r;
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    FlagIo h;
    ASSERT_TRUE(r.add(fds[0], EPOLLIN | EPOLLET, &h));
    char const c{'x'};
    ASSERT_EQ(::write(fds[1], &c, 1), 1);
    r.run_once(1000ms);
    EXPECT_EQ(h.count, 1);
    EXPECT_TRUE((h.last_events & static_cast<std::uint32_t>(EPOLLIN)) != 0u);
    r.del(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}

TEST(ReactorTest, SignalInvokesCallback) {
    Reactor r; // ctor 가 SIGINT/SIGTERM 블록 -> raise 는 signalfd 로 흐른다
    bool fired = false;
    r.on_signal([&fired] { fired = true; });
    ASSERT_EQ(::raise(SIGINT), 0);
    r.run_once(1000ms);
    EXPECT_TRUE(fired);
}

// ManualClock 주입 -> epoll 실시간 sleep 없이 타이머 발화를 결정적으로 검증.
// (run_once(0ms): 마감과 min 후 epoll_wait(0) 즉시 반환 -> expire(clock.now())로 판정.)
TEST(ReactorTest, TimerFiresDeterministicallyWithManualClock) {
    ddcs::common::ManualClock clk;
    Reactor r{clk};
    FlagTimer h;
    TimerId const id = r.schedule(5ms, &h);
    r.run_once(0ms); // now 그대로 -> 마감 전, 발화 없음
    EXPECT_EQ(h.count, 0);
    clk.advance(5ms);
    r.run_once(0ms); // now 전진 -> 마감 도달, 발화
    EXPECT_EQ(h.count, 1);
    EXPECT_EQ(h.last, id);
}
