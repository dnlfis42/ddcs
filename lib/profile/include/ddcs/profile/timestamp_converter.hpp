#pragma once

#include "ddcs/common/clock.hpp"

#include <cstdint>
#include <optional>

namespace ddcs::profile {

// 같은 monotonic Clock의 시각을 recording_origin 기준의 정수 ns로 바꾼다.
class TimestampConverter {
public:
    explicit TimestampConverter(common::Clock::time_point recording_origin) noexcept;

    // origin보다 앞선 시각은 tick 표본에 쓸 수 없으므로 nullopt를 반환한다.
    [[nodiscard]] std::optional<std::uint64_t>
    relative_ns(common::Clock::time_point time) const noexcept;

private:
    common::Clock::time_point recording_origin_;
};

} // namespace ddcs::profile
