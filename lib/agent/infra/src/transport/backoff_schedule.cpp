#include "ddcs/agent/infra/transport/backoff_schedule.hpp"

#include <algorithm>
#include <cstdint>

namespace ddcs::agent::infra::transport {

// xorshift32. 작은 결정적 RNG. 운영 시 ctor로 시드 주입 (부팅 시 한 번)
std::uint32_t BackoffSchedule::next_rand() noexcept {
    std::uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return x;
}

std::chrono::nanoseconds BackoffSchedule::next_delay() noexcept {
    auto const base = static_cast<std::uint64_t>(base_delay_.count());
    auto const cap = static_cast<std::uint64_t>(max_delay_.count());

    // base * 2^attempt, cap.
    std::uint64_t delay = base;
    for (std::uint32_t i = 0; i < attempt_ && delay < cap; ++i) {
        delay *= 2;
    }
    delay = std::min(delay, cap);

    // jitter: +/-jitter_percent% of delay, uniform.
    std::uint64_t const jitter_span = (delay * jitter_percent * 2) / 100; // 2 * percent (양쪽)
    std::uint64_t const offset = static_cast<std::uint64_t>(next_rand()) % (jitter_span + 1);
    std::uint64_t const lower = delay - delay * jitter_percent / 100;
    std::uint64_t const result = lower + offset;

    ++attempt_;
    return std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(result)};
}

} // namespace ddcs::agent::infra::transport
