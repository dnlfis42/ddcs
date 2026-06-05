#include "ddcs/runtime/detail/timer_queue.hpp"

namespace ddcs::runtime::detail {

void TimerQueue::push(time_point deadline, TimerId id) { heap_.push(Entry{deadline, id}); }

void TimerQueue::pop() noexcept {
    if (!heap_.empty()) {
        heap_.pop();
    }
}

std::optional<TimerQueue::Entry> TimerQueue::top() const {
    if (heap_.empty()) {
        return std::nullopt;
    }
    return heap_.top();
}

} // namespace ddcs::runtime::detail
