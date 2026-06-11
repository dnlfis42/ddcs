#include "ddcs/io/signal_source.hpp"

#include "ddcs/io/reactor.hpp"

#include <csignal>

#include <unistd.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

using ddcs::io::Reactor;
using ddcs::io::SignalSource;

class SignalMaskGuard {
public:
    SignalMaskGuard() { EXPECT_EQ(::sigprocmask(SIG_SETMASK, nullptr, &saved_mask_), 0); }
    ~SignalMaskGuard() { (void)::sigprocmask(SIG_SETMASK, &saved_mask_, nullptr); }

    SignalMaskGuard(SignalMaskGuard const&) = delete;
    SignalMaskGuard& operator=(SignalMaskGuard const&) = delete;
    SignalMaskGuard(SignalMaskGuard&&) = delete;
    SignalMaskGuard& operator=(SignalMaskGuard&&) = delete;

private:
    sigset_t saved_mask_{};
};

void unblock_signal(int signal) {
    sigset_t mask;
    ASSERT_EQ(::sigemptyset(&mask), 0);
    ASSERT_EQ(::sigaddset(&mask, signal), 0);
    ASSERT_EQ(::sigprocmask(SIG_UNBLOCK, &mask, nullptr), 0);
}

[[nodiscard]] bool signal_is_blocked(int signal) {
    sigset_t mask;
    if (::sigprocmask(SIG_SETMASK, nullptr, &mask) != 0) {
        return false;
    }
    return ::sigismember(&mask, signal) == 1;
}

} // namespace

TEST(SignalSourceTest, InvokesCallbackWithRaisedSignalNumber) {
    SignalMaskGuard signal_mask_guard;
    unblock_signal(SIGUSR1);

    Reactor reactor;
    int delivered{0};
    SignalSource source{reactor, {SIGUSR1}, [&](int signal) {
                            delivered = signal;
                            reactor.stop();
                        }};

    source.start();
    ASSERT_TRUE(source.active());
    ASSERT_EQ(::raise(SIGUSR1), 0);
    reactor.run_once(1000ms);

    EXPECT_EQ(delivered, SIGUSR1);

    source.stop();
    EXPECT_FALSE(source.active());
}

TEST(SignalSourceTest, StartsAndStopsIdempotently) {
    SignalMaskGuard signal_mask_guard;
    unblock_signal(SIGUSR1);

    Reactor reactor;
    SignalSource source{reactor, {SIGUSR1}, [](int) {}};

    source.start();
    source.start();
    EXPECT_TRUE(source.active());

    source.stop();
    source.stop();
    EXPECT_FALSE(source.active());
}

TEST(SignalSourceTest, RestoresSignalMaskWhenStopped) {
    SignalMaskGuard signal_mask_guard;
    unblock_signal(SIGUSR1);

    Reactor reactor;
    SignalSource source{reactor, {SIGUSR1}, [](int) {}};
    ASSERT_FALSE(signal_is_blocked(SIGUSR1));

    source.start();
    ASSERT_TRUE(signal_is_blocked(SIGUSR1));

    source.stop();

    EXPECT_FALSE(signal_is_blocked(SIGUSR1));
}

TEST(SignalSourceTest, StopsSafelyFromCallback) {
    SignalMaskGuard signal_mask_guard;
    unblock_signal(SIGUSR1);

    Reactor reactor;
    int delivered{0};
    SignalSource* source_ptr{nullptr};
    SignalSource source{reactor, {SIGUSR1}, [&](int signal) {
                            delivered = signal;
                            source_ptr->stop();
                        }};
    source_ptr = &source;

    source.start();
    ASSERT_EQ(::raise(SIGUSR1), 0);
    reactor.run_once(1000ms);

    EXPECT_EQ(delivered, SIGUSR1);
    EXPECT_FALSE(source.active());
}
