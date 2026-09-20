#pragma once

#include "ddcs/common/clock.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace ddcs::ctrl::detail {

// 이전 예약 시각에 주기를 더해 다음 실행을 예약한다. 지난 예약은 건너뛴다.
class FixedRateSchedule {
public:
    explicit FixedRateSchedule(std::chrono::nanoseconds interval)
        : interval_(interval) {
        if (interval_ <= common::Clock::duration::zero()) {
            throw std::invalid_argument("sweep interval must be positive");
        }
    }

    void start(common::Clock::time_point now) noexcept {
        deadline_ = now + interval_;
    }

    [[nodiscard]] common::Clock::time_point deadline() const noexcept {
        return deadline_;
    }

    [[nodiscard]] common::Clock::duration lateness(common::Clock::time_point now) const noexcept {
        return std::max(now - deadline_, common::Clock::duration::zero());
    }

    // 완료 시각 이하인 예약을 건너뛰고 그 횟수를 반환한다.
    [[nodiscard]] std::uint64_t advance(common::Clock::time_point finished) noexcept {
        deadline_ += interval_;
        if (deadline_ > finished) {
            return 0;
        }
        auto const skipped = (finished - deadline_) / interval_ + 1;
        deadline_ += interval_ * skipped;
        return static_cast<std::uint64_t>(skipped);
    }

private:
    common::Clock::duration interval_;
    common::Clock::time_point deadline_{};
};

} // namespace ddcs::ctrl::detail
