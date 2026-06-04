#include "ddcs/runtime/reactor.hpp"

#include "ddcs/runtime/fd_handler.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

#include <sys/epoll.h>
#include <unistd.h>

using namespace std::chrono_literals;

namespace {

using ddcs::runtime::FdHandler;
using ddcs::runtime::Reactor;

class FlagIo : public FdHandler {
public:
    int count{0};
    std::uint32_t last_events{0};
    void on_io(std::uint32_t events) override {
        ++count;
        last_events = events;
    }
};

} // namespace

TEST(ReactorTest, FdReadableDispatches) {
    Reactor r;
    int fds[2];
    ASSERT_EQ(::pipe(fds), 0);
    FlagIo h;
    ASSERT_TRUE(r.add(fds[0], EPOLLIN | EPOLLET, &h));
    char const c{'x'};
    ASSERT_EQ(::write(fds[1], &c, 1), 1);
    r.run_once(1000ms);
    EXPECT_EQ(h.count, 1);
    EXPECT_TRUE((h.last_events & static_cast<std::uint32_t>(EPOLLIN)) != 0u);
    r.del(fds[0]);
    ::close(fds[0]);
    ::close(fds[1]);
}
