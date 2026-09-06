#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ddcs::profile {

// steady_clock recording origin을 UTC에 대응시키기 위한 벽시계 bracket이다. 실제 origin은
// before_unix_ns와 after_unix_ns 사이에 있으며, system clock 조정은 별도 측정 오차다.
struct UtcClockBracket {
    std::uint64_t before_unix_ns;
    std::uint64_t after_unix_ns;
};

struct RunMetadata {
    // 빈 run_id는 측정 결과를 다른 실행과 구별할 수 없으므로 직렬화할 수 없다.
    std::string run_id;
    // Controller start() 호출 전후에 잡은 recording origin의 UTC bracket. 얻지 못했으면 null.
    std::optional<UtcClockBracket> recording_origin_utc = std::nullopt;
};

} // namespace ddcs::profile
