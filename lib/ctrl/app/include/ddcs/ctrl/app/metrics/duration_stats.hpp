#pragma once

#include <chrono>
#include <cstdint>

namespace ddcs::ctrl::app::metrics {

// 한 duration 계열의 통계 누산기(직전/최대/누적/횟수).
// record가 네 값을 세트로 갱신해 avg = sum/count 불변식을 지킨다.
struct DurationStats {
    std::uint64_t last_us{}; // 직전 표본
    std::uint64_t max_us{};  // 시작 후 최대
    std::uint64_t sum_us{};  // 누적(avg = /count)
    std::uint64_t count{};

    void record(std::chrono::steady_clock::duration work) noexcept {
        auto const us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(work).count()
        );
        last_us = us;
        if (us > max_us) {
            max_us = us;
        }
        sum_us += us;
        ++count;
    }
};

} // namespace ddcs::ctrl::app::metrics
