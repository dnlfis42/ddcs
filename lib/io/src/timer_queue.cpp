#include "ddcs/io/timer_queue.hpp"

namespace ddcs::io {

TimerId TimerQueue::schedule(time_point deadline, TimerHandler* handler) {
    TimerId const id{++next_id_};
    heap_.push(Entry{deadline, id, handler});
    live_.insert(id);
    return id;
}

void TimerQueue::cancel(TimerId id) noexcept {
    live_.erase(id); // heap 엔트리는 prune/expire 에서 stale 로 걸러짐
}

void TimerQueue::prune_cancelled() {
    while (!heap_.empty() && live_.find(heap_.top().id) == live_.end()) {
        heap_.pop(); // 취소된 선두 제거 -> 불필요한 wakeup/집계 방지
    }
}

std::optional<TimerQueue::time_point> TimerQueue::next_deadline() {
    prune_cancelled();
    if (heap_.empty()) {
        return std::nullopt;
    }
    return heap_.top().deadline;
}

void TimerQueue::expire(time_point now) {
    for (;;) {
        prune_cancelled(); // 매 회 선두를 live 로 정리(직전 fire 가 취소했을 수 있음)
        if (heap_.empty() || heap_.top().deadline > now) {
            break; // 더 이상 due 한 live 타이머 없음
        }
        Entry const e = heap_.top();
        heap_.pop();
        if (live_.erase(e.id) > 0) {   // prune 이 live 보장(방어적으로 재확인) - 소비
            e.handler->on_timer(e.id); // 핸들러는 취소된 타이머를 절대 보지 않음
        }
    }
}

} // namespace ddcs::io
