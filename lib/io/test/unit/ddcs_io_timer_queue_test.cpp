#include "ddcs/io/detail/timer_queue.hpp"

#include "ddcs/io/timer_token.hpp"

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

using ddcs::io::TimerToken;
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
    queue.push(base() + 5s, TimerToken{5});
    queue.push(base() + 2s, TimerToken{2});
    queue.push(base() + 9s, TimerToken{9});

    auto const timer = queue.top();
    ASSERT_TRUE(timer.has_value());
    EXPECT_EQ(timer->deadline, base() + 2s);
    EXPECT_EQ(timer->id, TimerToken{2});
}

TEST(TimerQueueTest, BreaksTiesWithTimerToken) {
    TimerQueue queue;
    queue.push(base() + 1s, TimerToken{2});
    queue.push(base() + 1s, TimerToken{1});

    auto const timer = queue.top();
    ASSERT_TRUE(timer.has_value());
    EXPECT_EQ(timer->id, TimerToken{1});
}

TEST(TimerQueueTest, RemovesTopWhenPopped) {
    TimerQueue queue;
    queue.push(base() + 1s, TimerToken{1});
    queue.push(base() + 3s, TimerToken{3});

    queue.pop();

    auto const timer = queue.top();
    ASSERT_TRUE(timer.has_value());
    EXPECT_EQ(timer->id, TimerToken{3});
}

TEST(TimerQueueTest, IgnoresPopWhenEmpty) {
    TimerQueue queue;

    queue.pop();

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.top().has_value());
}

TEST(TimerQueueTest, ClearsAllEntries) {
    TimerQueue queue;
    queue.push(base() + 1s, TimerToken{1});
    queue.push(base() + 3s, TimerToken{3});

    queue.clear();

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.top().has_value());
}

} // namespace
