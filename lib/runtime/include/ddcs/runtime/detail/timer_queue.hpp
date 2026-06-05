#pragma once

#include "ddcs/runtime/timer_id.hpp"

#include <chrono>
#include <optional>
#include <queue>
#include <vector>

namespace ddcs::runtime::detail {

// TimerId를 deadline 순서로 보관하는 min-heap. clock과 handler를 모른다.
class TimerQueue {
public:
    using time_point = std::chrono::steady_clock::time_point;

    struct Entry {
        time_point deadline;
        TimerId id;
    };

public:
    void push(time_point deadline, TimerId id);
    void pop() noexcept;

    [[nodiscard]]
    std::optional<Entry> top() const;
    [[nodiscard]]
    bool empty() const noexcept {
        return heap_.empty();
    }

private:
    struct Cmp {
        bool operator()(Entry const& a, Entry const& b) const noexcept { return a.deadline > b.deadline; }
    };

private:
    std::priority_queue<Entry, std::vector<Entry>, Cmp> heap_;
};

} // namespace ddcs::runtime::detail
