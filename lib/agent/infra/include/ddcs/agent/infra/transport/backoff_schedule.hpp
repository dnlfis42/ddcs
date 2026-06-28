#pragma once

#include <chrono>

#include <cstdint>

namespace ddcs::agent::infra::transport {

// 재연결 backoff 정책
// - exponential growth + jitter, cap 적용
// - 수열(jitter 무시): 1s, 2s, 4s, 8s, 16s, 30s, 30s, ...
//
// 호출자는 next_delay()로 timer 무장 후, app 등록(handshake) 성공 시 reset()
// - TCP 연결 성공이 아니라 등록 성공 기준 (등록 미완 사이클이 반복되면 backoff가 자라야 한다.)
class BackoffSchedule {
public:
    static constexpr std::chrono::nanoseconds default_base_delay = std::chrono::seconds{1};
    static constexpr std::chrono::nanoseconds default_max_delay = std::chrono::seconds{30};
    static constexpr std::uint32_t jitter_percent = 25; // +/-25%

public:
    BackoffSchedule() noexcept = default;
    explicit BackoffSchedule(std::uint32_t seed) noexcept
        : rng_state_(seed) {}
    // base/max 주입(설정 가능). 수열 base * 2^attempt 를 max로 cap.
    BackoffSchedule(std::chrono::nanoseconds base, std::chrono::nanoseconds max) noexcept
        : base_delay_(base),
          max_delay_(max) {}

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

private:
    std::chrono::nanoseconds base_delay_ = default_base_delay;
    std::chrono::nanoseconds max_delay_ = default_max_delay;
    std::uint32_t attempt_ = 0;
    std::uint32_t rng_state_ = 0xdeadbeefu; // 결정적 jitter(테스트 가능); 운영 시 ctor로 시드 주입
};

} // namespace ddcs::agent::infra::transport
