#pragma once

#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <chrono>
#include <optional>
#include <queue>
#include <unordered_set>
#include <vector>

#include <cstdint>

namespace ddcs::io {

// 절대-deadline min-heap + live_ 셋(lazy cancel). Clock 을 모른다 - '지금'은 호출자(Reactor)가 준다.
//
// 모든 소비자(session liveness/pw/command sweep/agent backoff)의 타이머가 여기 모이고,
// next_deadline() 이 전체를 한 점으로 집계한다 -> Reactor 가 epoll_wait 타임아웃 하나로 끝낸다.
// cancel 은 heap 을 건드리지 않고 live_ 에서만 지운다(lazy); 취소분은 prune/expire 에서 걸러진다.
class TimerQueue {
public:
    using time_point = std::chrono::steady_clock::time_point;

    // 절대 deadline 으로 예약. opaque TimerId 발급(1 부터). cancel 의 키.
    [[nodiscard]]
    TimerId schedule(time_point deadline, TimerHandler* handler);
    // O(1). heap 미접촉(lazy). 멱등.
    void cancel(TimerId id) noexcept;

    // 살아있는 가장 이른 deadline. 취소된 선두는 prune. 없으면 nullopt.
    [[nodiscard]]
    std::optional<time_point> next_deadline();

    // deadline <= now 인 live 타이머를 전부 fire(취소분은 조용히 skip). fd 이벤트 처리 뒤 1회.
    // 재진입 안전: 핸들러가 on_timer 안에서 schedule/cancel 해도 정합.
    void expire(time_point now);

private:
    struct Entry {
        time_point deadline;
        TimerId id;
        TimerHandler* handler;
    };
    struct Cmp {
        bool operator()(Entry const& a, Entry const& b) const noexcept {
            return a.deadline > b.deadline; // min-heap (이른 deadline 이 top)
        }
    };

    void prune_cancelled(); // 취소된 heap 선두 제거

    std::uint64_t next_id_{0}; // 발급 카운터(raw). 1 부터; TimerId{} 는 무효
    std::priority_queue<Entry, std::vector<Entry>, Cmp> heap_;
    std::unordered_set<TimerId> live_; // 존재 = 살아있음. erase = 취소.
};

} // namespace ddcs::io
