#include "ddcs/io/detail/channel_registry.hpp"

#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/fd.hpp"

#include <sys/eventfd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::io::Channel;
using ddcs::io::ChannelEvents;
using ddcs::io::ChannelHandler;
using ddcs::io::detail::ChannelRegistry;

class DummyHandler : public ChannelHandler {
public:
    void on_ready(Channel&, ChannelEvents) override {}
};

ddcs::io::Fd make_fd() {
    ddcs::io::Fd fd{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    EXPECT_TRUE(fd.valid());
    return fd;
}

TEST(ChannelRegistryTest, ResolvesInsertedChannel) {
    ChannelRegistry registry;
    DummyHandler handler;
    Channel channel;
    channel.init(make_fd(), ChannelEvents::readable, handler);

    auto const token = registry.insert(channel);

    EXPECT_EQ(registry.resolve(token), &channel);
}

TEST(ChannelRegistryTest, ReturnsCurrentTokenForChannel) {
    ChannelRegistry registry;
    DummyHandler handler;
    Channel channel;
    channel.init(make_fd(), ChannelEvents::readable, handler);

    auto const inserted_token = registry.insert(channel);
    auto const current_token = registry.token(channel);

    EXPECT_EQ(current_token, inserted_token);
    EXPECT_EQ(registry.resolve(current_token), &channel);
}

TEST(ChannelRegistryTest, InvalidatesTokenWhenErased) {
    ChannelRegistry registry;
    DummyHandler handler;
    Channel channel;
    channel.init(make_fd(), ChannelEvents::readable, handler);

    auto const token = registry.insert(channel);
    EXPECT_TRUE(registry.erase(channel));

    EXPECT_EQ(registry.resolve(token), nullptr);
}

TEST(ChannelRegistryTest, ReturnsFalseWhenErasingUnknownChannel) {
    ChannelRegistry registry;
    DummyHandler handler;
    Channel channel;
    channel.init(make_fd(), ChannelEvents::readable, handler);

    EXPECT_FALSE(registry.erase(channel));
}

} // namespace
