#include "ddcs/io/channel.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"

#include <utility>

#include <cerrno>

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

ddcs::common::Fd make_fd() {
    ddcs::common::Fd fd{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    EXPECT_TRUE(fd.valid());
    return fd;
}

[[nodiscard]] bool fd_is_closed(int fd) {
    errno = 0;
    return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

} // namespace

TEST(ChannelTest, InitializesWithOwnedFd) {
    DummyHandler handler;
    Channel channel;
    ddcs::common::Fd fd = make_fd();
    int const raw_fd = fd.get();

    ASSERT_TRUE(channel.init(std::move(fd), ChannelEvents::readable, handler));

    EXPECT_FALSE(fd.valid());
    EXPECT_TRUE(channel.valid());
    EXPECT_FALSE(channel.registered());
    EXPECT_EQ(channel.fd(), raw_fd);
    EXPECT_EQ(channel.interests(), ChannelEvents::readable);
    EXPECT_EQ(&channel.handler(), &handler);
}

TEST(ChannelTest, RejectsInvalidFd) {
    DummyHandler handler;
    Channel channel;
    ddcs::common::Fd fd;

    EXPECT_FALSE(channel.init(std::move(fd), ChannelEvents::readable, handler));

    EXPECT_FALSE(channel.valid());
    EXPECT_FALSE(channel.registered());
    EXPECT_EQ(channel.fd(), ddcs::common::Fd::invalid);
    EXPECT_EQ(channel.interests(), ChannelEvents::none);
}

TEST(ChannelTest, DoesNotConsumeFdWhenAlreadyInitialized) {
    DummyHandler handler;
    Channel channel;
    ASSERT_TRUE(channel.init(make_fd(), ChannelEvents::readable, handler));

    ddcs::common::Fd fd = make_fd();
    int const raw_fd = fd.get();

    EXPECT_FALSE(channel.init(std::move(fd), ChannelEvents::writable, handler));

    EXPECT_TRUE(fd.valid());
    EXPECT_EQ(fd.get(), raw_fd);
    EXPECT_EQ(channel.interests(), ChannelEvents::readable);
}

TEST(ChannelTest, ResetsAndClosesFd) {
    DummyHandler handler;
    Channel channel;
    ASSERT_TRUE(channel.init(make_fd(), ChannelEvents::readable, handler));
    int const raw_fd = channel.fd();

    channel.reset();

    EXPECT_FALSE(channel.valid());
    EXPECT_FALSE(channel.registered());
    EXPECT_EQ(channel.fd(), ddcs::common::Fd::invalid);
    EXPECT_EQ(channel.interests(), ChannelEvents::none);
    EXPECT_TRUE(fd_is_closed(raw_fd));
}

TEST(ChannelTest, ReinitializesAfterReset) {
    DummyHandler handler;
    Channel channel;
    ASSERT_TRUE(channel.init(make_fd(), ChannelEvents::readable, handler));

    channel.reset();

    ASSERT_TRUE(channel.init(make_fd(), ChannelEvents::writable, handler));
    EXPECT_TRUE(channel.valid());
    EXPECT_EQ(channel.interests(), ChannelEvents::writable);
}
