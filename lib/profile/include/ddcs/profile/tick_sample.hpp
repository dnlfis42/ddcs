#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ddcs::profile {

inline constexpr std::string_view tick_profile_schema_name = "ddcs.tick_profile";
inline constexpr std::uint32_t tick_profile_schema_version = 1;
inline constexpr std::string_view tick_profile_time_unit = "ns";

enum class TickOutcome : std::uint8_t {
    completed = 0,
    command_sweep_threw = 1,
    session_sweep_threw = 2,
    policy_evaluate_threw = 3,
};

[[nodiscard]] constexpr bool is_known_outcome(TickOutcome outcome) noexcept {
    switch (outcome) {
    case TickOutcome::completed:
    case TickOutcome::command_sweep_threw:
    case TickOutcome::session_sweep_threw:
    case TickOutcome::policy_evaluate_threw:
        return true;
    }

    return false;
}

struct TickSample {
    std::uint64_t tick_id;
    std::uint64_t started_ns;
    std::uint64_t command_sweep_ended_ns;
    std::uint64_t session_sweep_ended_ns;
    std::uint64_t policy_evaluate_ended_ns;
    std::uint64_t finished_ns;
    TickOutcome outcome;
};

[[nodiscard]] constexpr std::string_view to_string(TickOutcome outcome) noexcept {
    switch (outcome) {
    case TickOutcome::completed:
        return "completed";
    case TickOutcome::command_sweep_threw:
        return "command_sweep_threw";
    case TickOutcome::session_sweep_threw:
        return "session_sweep_threw";
    case TickOutcome::policy_evaluate_threw:
        return "policy_evaluate_threw";
    }

    return "unknown";
}

[[nodiscard]] constexpr bool command_sweep_completed(TickOutcome outcome) noexcept {
    return outcome == TickOutcome::completed || outcome == TickOutcome::session_sweep_threw ||
           outcome == TickOutcome::policy_evaluate_threw;
}

[[nodiscard]] constexpr bool session_sweep_completed(TickOutcome outcome) noexcept {
    return outcome == TickOutcome::completed || outcome == TickOutcome::policy_evaluate_threw;
}

[[nodiscard]] constexpr bool policy_evaluate_completed(TickOutcome outcome) noexcept {
    return outcome == TickOutcome::completed;
}

// outcome이 지정한 완료 경계와 시각 순서를 함께 확인한다. 0은 유효한 상대 시각이므로,
// 미완료 경계의 0은 outcome과 같이 해석한다.
[[nodiscard]] constexpr bool is_valid_tick_sample(TickSample const& sample) noexcept {
    if (sample.tick_id == 0) {
        return false;
    }

    switch (sample.outcome) {
    case TickOutcome::completed:
        return sample.started_ns <= sample.command_sweep_ended_ns &&
               sample.command_sweep_ended_ns <= sample.session_sweep_ended_ns &&
               sample.session_sweep_ended_ns <= sample.policy_evaluate_ended_ns &&
               sample.policy_evaluate_ended_ns == sample.finished_ns;
    case TickOutcome::command_sweep_threw:
        return sample.command_sweep_ended_ns == 0 && sample.session_sweep_ended_ns == 0 &&
               sample.policy_evaluate_ended_ns == 0 && sample.started_ns <= sample.finished_ns;
    case TickOutcome::session_sweep_threw:
        return sample.session_sweep_ended_ns == 0 && sample.policy_evaluate_ended_ns == 0 &&
               sample.started_ns <= sample.command_sweep_ended_ns &&
               sample.command_sweep_ended_ns <= sample.finished_ns;
    case TickOutcome::policy_evaluate_threw:
        return sample.policy_evaluate_ended_ns == 0 &&
               sample.started_ns <= sample.command_sweep_ended_ns &&
               sample.command_sweep_ended_ns <= sample.session_sweep_ended_ns &&
               sample.session_sweep_ended_ns <= sample.finished_ns;
    }

    return false;
}

static_assert(std::is_trivially_copyable_v<TickSample>);

} // namespace ddcs::profile
