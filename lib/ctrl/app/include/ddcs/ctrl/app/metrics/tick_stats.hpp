#pragma once

#include "ddcs/ctrl/app/metrics/duration_stats.hpp"

#include <cstdint>

namespace ddcs::ctrl::app::metrics {

struct TickStats {
    DurationStats work;           // 완료한 tick의 작업 시간
    DurationStats start_lateness; // 실패한 tick도 포함한 실행 시작 지연
    std::uint64_t skipped_total{};
};

} // namespace ddcs::ctrl::app::metrics
