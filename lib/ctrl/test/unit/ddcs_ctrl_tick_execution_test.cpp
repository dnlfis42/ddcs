#include "ddcs/ctrl/detail/tick_execution.hpp"

#include <exception>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

struct Calls {
    int command = 0;
    int session = 0;
    int policy = 0;
};

enum class ThrowAt {
    command,
    session,
    policy,
};

void expect_failure(
    ThrowAt throw_at, ddcs::profile::TickOutcome expected_outcome, Calls expected_calls
) {
    Calls calls;
    auto const failure = ddcs::ctrl::detail::execute_tick_phases(
        [&] {
            ++calls.command;
            if (throw_at == ThrowAt::command) {
                throw std::runtime_error{"command failure"};
            }
        },
        [&] {
            ++calls.session;
            if (throw_at == ThrowAt::session) {
                throw std::runtime_error{"session failure"};
            }
        },
        [&] {
            ++calls.policy;
            if (throw_at == ThrowAt::policy) {
                throw std::runtime_error{"policy failure"};
            }
        }
    );

    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(failure->outcome, expected_outcome);
    ASSERT_NE(failure->exception, nullptr);
    EXPECT_THROW(std::rethrow_exception(failure->exception), std::runtime_error);
    EXPECT_EQ(calls.command, expected_calls.command);
    EXPECT_EQ(calls.session, expected_calls.session);
    EXPECT_EQ(calls.policy, expected_calls.policy);
}

TEST(TickExecutionTest, RunsAllPhasesInOrderWhenTheySucceed) {
    Calls calls;
    auto const failure = ddcs::ctrl::detail::execute_tick_phases(
        [&] { ++calls.command; }, [&] { ++calls.session; }, [&] { ++calls.policy; }
    );

    EXPECT_FALSE(failure.has_value());
    EXPECT_EQ(calls.command, 1);
    EXPECT_EQ(calls.session, 1);
    EXPECT_EQ(calls.policy, 1);
}

TEST(TickExecutionTest, StopsAtTheThrowingPhaseAndPreservesItsProfileOutcome) {
    expect_failure(
        ThrowAt::command, ddcs::profile::TickOutcome::command_sweep_threw,
        Calls{.command = 1, .session = 0, .policy = 0}
    );
    expect_failure(
        ThrowAt::session, ddcs::profile::TickOutcome::session_sweep_threw,
        Calls{.command = 1, .session = 1, .policy = 0}
    );
    expect_failure(
        ThrowAt::policy, ddcs::profile::TickOutcome::policy_evaluate_threw,
        Calls{.command = 1, .session = 1, .policy = 1}
    );
}

} // namespace
