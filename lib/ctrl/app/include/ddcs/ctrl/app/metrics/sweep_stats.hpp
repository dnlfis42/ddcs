#pragma once

#include <chrono>
#include <cstdint>

namespace ddcs::ctrl::app::metrics {

// 주기 sweep tick의 작업 소요시간 통계(단일 스레드 포화 관측용).
// 컨트롤러 루프(on_expired)가 매 tick 작업 시간을 record하고 MetricsService가 읽는다.
// sweep는 한 스레드에서 도는 도메인 작업 전부(명령 재전송 + monitor sweep + 정책 평가)라,
// 이 시간이 sweep 주기에 근접하면 리액터가 포화한 것이다 -- agent 수 대비 핵심 한계 지표.
class SweepStats {
public:
    void record(std::chrono::steady_clock::duration work) noexcept {
        auto const us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(work).count()
        );
        last_us_ = us;
        if (us > max_us_) {
            max_us_ = us;
        }
        sum_us_ += us;
        ++ticks_;
    }

    [[nodiscard]] std::uint64_t last_us() const noexcept {
        return last_us_;
    } // 직전 tick
    [[nodiscard]] std::uint64_t max_us() const noexcept {
        return max_us_;
    } // 시작 후 최대
    [[nodiscard]] std::uint64_t sum_us() const noexcept {
        return sum_us_;
    } // 누적(avg = /ticks)
    [[nodiscard]] std::uint64_t ticks() const noexcept {
        return ticks_;
    }

private:
    std::uint64_t last_us_{};
    std::uint64_t max_us_{};
    std::uint64_t sum_us_{};
    std::uint64_t ticks_{};
};

} // namespace ddcs::ctrl::app::metrics
