#pragma once

#include "ddcs/profile/tick_sample.hpp"

#include <exception>
#include <optional>
#include <utility>

namespace ddcs::ctrl::detail {

// 한 Controller tick의 세 phase를 순서대로 실행한 실패 결과. 실패 시 이후 phase는 절대 실행하지
// 않고, 원래 예외와 raw profile outcome을 함께 돌려준다.
struct TickPhaseFailure {
    profile::TickOutcome outcome;
    std::exception_ptr exception;
};

// 세 callable은 각각 command, session, policy phase다. 정상 완료면 nullopt, 예외면 해당
// phase의 profile outcome과 원래 예외를 반환한다. Timer 재예약과 metric/profile 기록은 호출자가
// 소유하므로 이 module은 실행 순서와 실패 분류만 숨긴다.
template <typename CommandSweep, typename SessionSweep, typename PolicyEvaluate>
[[nodiscard]] std::optional<TickPhaseFailure> execute_tick_phases(
    CommandSweep&& command_sweep, SessionSweep&& session_sweep, PolicyEvaluate&& policy_evaluate
) {
    try {
        std::forward<CommandSweep>(command_sweep)();
    } catch (...) {
        return TickPhaseFailure{
            .outcome = profile::TickOutcome::command_sweep_threw,
            .exception = std::current_exception(),
        };
    }

    try {
        std::forward<SessionSweep>(session_sweep)();
    } catch (...) {
        return TickPhaseFailure{
            .outcome = profile::TickOutcome::session_sweep_threw,
            .exception = std::current_exception(),
        };
    }

    try {
        std::forward<PolicyEvaluate>(policy_evaluate)();
    } catch (...) {
        return TickPhaseFailure{
            .outcome = profile::TickOutcome::policy_evaluate_threw,
            .exception = std::current_exception(),
        };
    }

    return std::nullopt;
}

} // namespace ddcs::ctrl::detail
