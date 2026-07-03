#pragma once

#include <chrono>
#include <cstdint>

namespace ddcs::ctrl::app::metrics {

// 주기 sweep tick의 작업 소요시간 통계(단일 스레드 포화 관측용).
// 컨트롤러 루프(on_expired)가 매 tick 작업 시간을 record하고 MetricsService가 읽는다.
// sweep는 한 스레드에서 도는 도메인 작업 전부(명령 재전송 + monitor sweep + 정책 평가)라,
// 이 시간이 sweep 주기에 근접하면 리액터가 포화한 것이다 -- agent 수 대비 핵심 한계 지표.
struct SweepStats {
    std::uint64_t last_us{}; // 직전 tick
    std::uint64_t max_us{};  // 시작 후 최대
    std::uint64_t sum_us{};  // 누적(avg = /ticks)
    std::uint64_t ticks{};

    void record(std::chrono::steady_clock::duration work) noexcept {
        auto const us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(work).count()
        );
        last_us = us;
        if (us > max_us) {
            max_us = us;
        }
        sum_us += us;
        ++ticks;
    }
};

} // namespace ddcs::ctrl::app::metrics
