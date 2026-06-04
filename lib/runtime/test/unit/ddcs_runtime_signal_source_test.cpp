#include "ddcs/runtime/signal_source.hpp"

#include "ddcs/runtime/reactor.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <csignal>

#include <unistd.h>

using namespace std::chrono_literals;

namespace {

using ddcs::runtime::Reactor;
using ddcs::runtime::SignalSource;

} // namespace

TEST(SignalSourceTest, RaisedSignalInvokesCallback) {
    Reactor reactor;
    bool fired{false};
    SignalSource source{reactor, {SIGUSR1}, [&] {
                            fired = true;
                            reactor.stop();
                        }};

    source.start();
    ASSERT_EQ(::raise(SIGUSR1), 0);
    reactor.run_once(1000ms);

    EXPECT_TRUE(fired);
}
