#include "ddcs/runtime/detail/timer_queue.hpp"

#include "ddcs/runtime/timer_id.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

namespace {

using ddcs::runtime::TimerId;
using ddcs::runtime::detail::TimerQueue;
using time_point = TimerQueue::time_point;
using namespace std::chrono_literals;

time_point base() { return time_point{} + 1000s; }

} // namespace

TEST(TimerQueueTest, TopEmptyReturnsNullopt) {
    TimerQueue q;
    EXPECT_FALSE(q.top().has_value());
}

TEST(TimerQueueTest, TopReturnsEarliestDeadline) {
    TimerQueue q;
    q.push(base() + 5s, TimerId{5});
    q.push(base() + 2s, TimerId{2});
    q.push(base() + 9s, TimerId{9});

    auto const entry = q.top();
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->deadline, base() + 2s);
    EXPECT_EQ(entry->id, TimerId{2});
}

TEST(TimerQueueTest, PopRemovesEarliestEntry) {
    TimerQueue q;
    q.push(base() + 1s, TimerId{1});
    q.push(base() + 3s, TimerId{3});

    q.pop();

    auto const entry = q.top();
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->deadline, base() + 3s);
    EXPECT_EQ(entry->id, TimerId{3});
}

TEST(TimerQueueTest, PopOnEmptyIsNoop) {
    TimerQueue q;
    q.pop();
    EXPECT_TRUE(q.empty());
}

TEST(TimerQueueTest, PopsInDeadlineOrder) {
    TimerQueue q;
    q.push(base() + 3s, TimerId{3});
    q.push(base() + 1s, TimerId{1});
    q.push(base() + 2s, TimerId{2});

    std::vector<TimerId> ids;
    while (auto const entry = q.top()) {
        ids.push_back(entry->id);
        q.pop();
    }

    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], TimerId{1});
    EXPECT_EQ(ids[1], TimerId{2});
    EXPECT_EQ(ids[2], TimerId{3});
}
