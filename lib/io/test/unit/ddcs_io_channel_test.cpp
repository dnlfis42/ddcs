#include "ddcs/io/channel.hpp"

#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/fd.hpp"

#include <cerrno>
#include <utility>

#include <fcntl.h>
#include <sys/eventfd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::io::Channel;
using ddcs::io::ChannelEvents;
using ddcs::io::ChannelHandler;

class DummyHandler : public ChannelHandler {
public:
    void on_ready(Channel&, ChannelEvents) override {}
};

ddcs::io::Fd make_fd() {
    ddcs::io::Fd fd{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    EXPECT_TRUE(fd.valid());
    return fd;
}

[[nodiscard]] bool fd_is_closed(int fd) {
    errno = 0;
    return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

TEST(ChannelTest, InitializesWithOwnedFd) {
    DummyHandler handler;
    Channel channel;
    ddcs::io::Fd fd = make_fd();
    int const raw_fd = fd.get();

    channel.init(std::move(fd), ChannelEvents::readable, handler);

    EXPECT_FALSE(fd.valid());
    EXPECT_TRUE(channel.valid());
    EXPECT_FALSE(channel.registered());
    EXPECT_EQ(channel.fd(), raw_fd);
    EXPECT_EQ(channel.interests(), ChannelEvents::readable);
    EXPECT_EQ(&channel.handler(), &handler);
}

// 무효 fd / 이중 init은 프로그래머 오류 전제조건이라 죽는다 (assert + terminate)
TEST(ChannelDeathTest, DiesOnInvalidFd) {
    DummyHandler handler;
    Channel channel;
    ddcs::io::Fd fd;

    EXPECT_DEATH(channel.init(std::move(fd), ChannelEvents::readable, handler), "");
}

TEST(ChannelDeathTest, DiesWhenAlreadyInitialized) {
    DummyHandler handler;
    Channel channel;
    channel.init(make_fd(), ChannelEvents::readable, handler);

    EXPECT_DEATH(channel.init(make_fd(), ChannelEvents::writable, handler), "");
}

TEST(ChannelTest, ClosesFd) {
    DummyHandler handler;
    Channel channel;
    channel.init(make_fd(), ChannelEvents::readable, handler);
    int const raw_fd = channel.fd();

    channel.close();

    EXPECT_FALSE(channel.valid());
    EXPECT_FALSE(channel.registered());
    EXPECT_EQ(channel.fd(), ddcs::io::Fd::invalid);
    EXPECT_EQ(channel.interests(), ChannelEvents::none);
    EXPECT_TRUE(fd_is_closed(raw_fd));
}

TEST(ChannelTest, ReinitializesAfterClose) {
    DummyHandler handler;
    Channel channel;
    channel.init(make_fd(), ChannelEvents::readable, handler);

    channel.close();

    channel.init(make_fd(), ChannelEvents::writable, handler);
    EXPECT_TRUE(channel.valid());
    EXPECT_EQ(channel.interests(), ChannelEvents::writable);
}

} // namespace
