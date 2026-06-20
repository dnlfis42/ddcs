#include "ddcs/io/detail/timer_queue.hpp"

#include "ddcs/io/timer_id.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

using ddcs::io::TimerId;
using ddcs::io::detail::TimerQueue;
using time_point = TimerQueue::time_point;

time_point base() {
    return time_point{} + 1000s;
}

TEST(TimerQueueTest, ReturnsNulloptWhenEmpty) {
    TimerQueue queue;

    EXPECT_FALSE(queue.top().has_value());
    EXPECT_TRUE(queue.empty());
}

TEST(TimerQueueTest, ReturnsEarliestDeadlineOnTop) {
    TimerQueue queue;
    queue.push(base() + 5s, TimerId{5});
    queue.push(base() + 2s, TimerId{2});
    queue.push(base() + 9s, TimerId{9});

    auto const timer = queue.top();
    ASSERT_TRUE(timer.has_value());
    EXPECT_EQ(timer->deadline, base() + 2s);
    EXPECT_EQ(timer->id, TimerId{2});
}

TEST(TimerQueueTest, BreaksTiesWithTimerId) {
    TimerQueue queue;
    queue.push(base() + 1s, TimerId{2});
    queue.push(base() + 1s, TimerId{1});

    auto const timer = queue.top();
    ASSERT_TRUE(timer.has_value());
    EXPECT_EQ(timer->id, TimerId{1});
}

TEST(TimerQueueTest, RemovesTopWhenPopped) {
    TimerQueue queue;
    queue.push(base() + 1s, TimerId{1});
    queue.push(base() + 3s, TimerId{3});

    queue.pop();

    auto const timer = queue.top();
    ASSERT_TRUE(timer.has_value());
    EXPECT_EQ(timer->id, TimerId{3});
}

TEST(TimerQueueTest, IgnoresPopWhenEmpty) {
    TimerQueue queue;

    queue.pop();

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.top().has_value());
}

TEST(TimerQueueTest, ClearsAllEntries) {
    TimerQueue queue;
    queue.push(base() + 1s, TimerId{1});
    queue.push(base() + 3s, TimerId{3});

    queue.clear();

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.top().has_value());
}

} // namespace
