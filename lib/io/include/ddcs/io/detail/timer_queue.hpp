#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/io/timer_token.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <vector>

namespace ddcs::io::detail {

// TimerToken을 deadline 순서로 보관하는 min-heap
class TimerQueue {
public:
    using time_point = common::Clock::time_point;

    struct Entry {
        time_point deadline;
        TimerToken id;
    };

    [[nodiscard]] bool empty() const noexcept {
        return heap_.empty();
    }

    [[nodiscard]] std::optional<Entry> top() const noexcept {
        if (heap_.empty()) {
            return std::nullopt;
        }
        return heap_.front();
    }

    void push(time_point deadline, TimerToken id) {
        assert(id.valid());

        heap_.push_back(Entry{deadline, id});
        std::push_heap(heap_.begin(), heap_.end(), Later{});
    }

    void pop() noexcept {
        if (heap_.empty()) {
            return;
        }

        std::pop_heap(heap_.begin(), heap_.end(), Later{});
        heap_.pop_back();
    }

    void clear() noexcept {
        heap_.clear();
    }

private:
    // deadline이 같으면 id가 작은(먼저 등록된) 쪽을 먼저 만료시키는 min-heap 비교자
    struct Later {
        bool operator()(Entry const& lhs, Entry const& rhs) const noexcept {
            if (lhs.deadline != rhs.deadline) {
                return lhs.deadline > rhs.deadline;
            }
            return lhs.id.get() > rhs.id.get();
        }
    };

    std::vector<Entry> heap_;
};

} // namespace ddcs::io::detail
