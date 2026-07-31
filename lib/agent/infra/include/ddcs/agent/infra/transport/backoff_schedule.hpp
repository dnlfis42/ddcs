#pragma once

#include <chrono>

#include <cstdint>

namespace ddcs::agent::infra::transport {

// 재연결 backoff 정책 (지수 증가 + jitter, cap 적용)
class BackoffSchedule {
public:
    static constexpr std::uint32_t jitter_percent = 25; // +/-25%

    // base/max의 단일 출처는 Agent::Config라 기본값 없이 명시 주입만 받는다.
    // 수열 base * 2^attempt 를 max로 cap.
    // seed는 jitter 수열의 시작점. 운영은 조립 루트가 비결정 값을, 테스트는 고정값을 준다.
    BackoffSchedule(
        std::chrono::nanoseconds base, std::chrono::nanoseconds max, std::uint32_t seed
    ) noexcept
        : base_delay_(base),
          max_delay_(max),
          rng_state_(seed) {}

    std::uint32_t attempt() const noexcept {
        return attempt_;
    }

    // 다음 backoff 지연. 호출마다 attempt++
    std::chrono::nanoseconds next_delay() noexcept;

    // app 등록(handshake) 성공 시 호출. attempt 카운터 0으로
    void reset() noexcept {
        attempt_ = 0;
    }

private:
    std::uint32_t next_rand() noexcept;

    std::chrono::nanoseconds base_delay_;
    std::chrono::nanoseconds max_delay_;
    std::uint32_t attempt_ = 0;
    std::uint32_t rng_state_; // xorshift32 상태. 같은 seed면 같은 jitter 수열(테스트 가능)
};

} // namespace ddcs::agent::infra::transport
