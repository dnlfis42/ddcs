#include "ddcs/io/timer_queue.hpp"

#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

namespace {

using ddcs::io::TimerHandler;
using ddcs::io::TimerId;
using ddcs::io::TimerQueue;
using time_point = TimerQueue::time_point;
using namespace std::chrono_literals;

time_point base() { return time_point{} + 1000s; } // 임의 양수 기준점

struct RecordingHandler : TimerHandler {
    std::vector<TimerId> fired;
    void on_timer(TimerId id) override { fired.push_back(id); }
};

// 첫 발화 때 미래 타이머 1개를 추가(재진입 안전성 검증용).
struct ReschedulingHandler : TimerHandler {
    TimerQueue* queue{nullptr};
    time_point when{};
    int count{0};
    void on_timer(TimerId /*id*/) override {
        if (count == 0) {
            (void)queue->schedule(when, this);
        }
        ++count;
    }
};

} // namespace

TEST(TimerQueueTest, NextDeadlineEmptyReturnsNullopt) {
    TimerQueue q;
    EXPECT_FALSE(q.next_deadline().has_value());
}

TEST(TimerQueueTest, NextDeadlineReturnsEarliest) {
    TimerQueue q;
    RecordingHandler h;
    (void)q.schedule(base() + 5s, &h);
    (void)q.schedule(base() + 2s, &h);
    (void)q.schedule(base() + 9s, &h);
    auto const dl = q.next_deadline();
    ASSERT_TRUE(dl.has_value());
    EXPECT_EQ(*dl, base() + 2s);
}

TEST(TimerQueueTest, ExpireFiresOnlyDueTimers) {
    TimerQueue q;
    RecordingHandler h;
    TimerId const id = q.schedule(base() + 1s, &h);
    q.expire(base()); // 마감 전 - 발화 없음
    EXPECT_TRUE(h.fired.empty());
    q.expire(base() + 1s); // 마감 도달 - 발화
    ASSERT_EQ(h.fired.size(), 1u);
    EXPECT_EQ(h.fired[0], id);
}

TEST(TimerQueueTest, ExpireFiresInDeadlineOrder) {
    TimerQueue q;
    RecordingHandler h;
    TimerId const id3 = q.schedule(base() + 3s, &h);
    TimerId const id1 = q.schedule(base() + 1s, &h);
    TimerId const id2 = q.schedule(base() + 2s, &h);
    q.expire(base() + 5s);
    ASSERT_EQ(h.fired.size(), 3u);
    EXPECT_EQ(h.fired[0], id1);
    EXPECT_EQ(h.fired[1], id2);
    EXPECT_EQ(h.fired[2], id3);
}

TEST(TimerQueueTest, CancelledTimerDoesNotFire) {
    TimerQueue q;
    RecordingHandler h;
    TimerId const id = q.schedule(base() + 1s, &h);
    q.cancel(id);
    q.expire(base() + 10s);
    EXPECT_TRUE(h.fired.empty());
}

TEST(TimerQueueTest, CancelPrunesNextDeadline) {
    TimerQueue q;
    RecordingHandler h;
    TimerId const early = q.schedule(base() + 1s, &h);
    (void)q.schedule(base() + 5s, &h);
    q.cancel(early);
    auto const dl = q.next_deadline();
    ASSERT_TRUE(dl.has_value());
    EXPECT_EQ(*dl, base() + 5s); // 취소된 선두는 prune -> 다음 것이 집계됨
}

TEST(TimerQueueTest, ExpireToleratesRescheduleDuringFire) {
    TimerQueue q;
    ReschedulingHandler h;
    h.queue = &q;
    h.when = base() + 10s;
    (void)q.schedule(base(), &h); // 즉시 due
    q.expire(base() + 1s);        // due 1개 발화 -> 핸들러가 base+10s 예약
    EXPECT_EQ(h.count, 1);        // 미래 타이머는 이 expire 에서 안 울림
    q.expire(base() + 11s);       // 이제 발화
    EXPECT_EQ(h.count, 2);
}

TEST(TimerQueueTest, CancelIsIdempotent) {
    TimerQueue q;
    RecordingHandler h;
    q.cancel(TimerId{999}); // 미등록
    TimerId const id = q.schedule(base() + 1s, &h);
    q.cancel(id);
    q.cancel(id); // 두 번
    q.expire(base() + 5s);
    EXPECT_TRUE(h.fired.empty());
}
