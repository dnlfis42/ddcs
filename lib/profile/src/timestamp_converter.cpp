#include "ddcs/profile/timestamp_converter.hpp"

#include <chrono>

namespace ddcs::profile {

TimestampConverter::TimestampConverter(common::Clock::time_point recording_origin) noexcept
    : recording_origin_(recording_origin) {}

std::optional<std::uint64_t>
TimestampConverter::relative_ns(common::Clock::time_point time) const noexcept {
    auto const elapsed = time - recording_origin_;
    if (elapsed < common::Clock::duration::zero()) {
        return std::nullopt;
    }

    auto const ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    if (ns < 0) {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(ns);
}

} // namespace ddcs::profile
