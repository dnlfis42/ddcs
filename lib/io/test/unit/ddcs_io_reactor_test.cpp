#include "ddcs/io/reactor.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"

#include <utility>

#include <unistd.h>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

namespace {

using ddcs::io::Channel;
using ddcs::io::ChannelEvents;
using ddcs::io::ChannelHandler;
using ddcs::io::Reactor;

struct PipeFds {
    ddcs::common::Fd read;
    ddcs::common::Fd write;
};

class FlagHandler : public ChannelHandler {
public:
    void on_ready(Channel&, ChannelEvents events) override {
        ++count;
        last_events = events;
    }

    int count = 0;
    ChannelEvents last_events = ChannelEvents::none;
};

PipeFds make_pipe() {
    int fds[2]{};
    if (::pipe(fds) != 0) {
        ADD_FAILURE() << "pipe failed";
        return {};
    }
    return PipeFds{ddcs::common::Fd{fds[0]}, ddcs::common::Fd{fds[1]}};
}

void write_byte(ddcs::common::Fd const& fd) {
    char const c{'x'};
    ASSERT_EQ(::write(fd.get(), &c, 1), 1);
}

TEST(ReactorTest, RejectsInvalidChannel) {
    Reactor reactor;
    Channel channel;

    EXPECT_FALSE(reactor.add(channel));
    EXPECT_FALSE(reactor.modify(channel, ChannelEvents::readable));
}

TEST(ReactorTest, DispatchesReadableChannel) {
    Reactor reactor;
    PipeFds fds = make_pipe();
    FlagHandler handler;
    Channel channel;
    ASSERT_TRUE(channel.init(
        std::move(fds.read), ChannelEvents::readable | ChannelEvents::edge_triggered, handler
    ));
    ASSERT_TRUE(reactor.add(channel));

    write_byte(fds.write);
    reactor.run_once(1000ms);

    EXPECT_EQ(handler.count, 1);
    EXPECT_TRUE(contains(handler.last_events, ChannelEvents::readable));

    reactor.remove(channel);
}

TEST(ReactorTest, UpdatesInterestsWhenModified) {
    Reactor reactor;
    PipeFds fds = make_pipe();
    FlagHandler handler;
    Channel channel;
    ASSERT_TRUE(channel.init(
        std::move(fds.read), ChannelEvents::readable | ChannelEvents::edge_triggered, handler
    ));
    ASSERT_TRUE(reactor.add(channel));

    ASSERT_TRUE(reactor.modify(channel, ChannelEvents::writable | ChannelEvents::edge_triggered));
    EXPECT_EQ(channel.interests(), ChannelEvents::writable | ChannelEvents::edge_triggered);

    write_byte(fds.write);
    reactor.run_once(10ms);

    EXPECT_EQ(handler.count, 0);

    reactor.remove(channel);
}

TEST(ReactorTest, StopsDispatchingAfterRemove) {
    Reactor reactor;
    PipeFds fds = make_pipe();
    FlagHandler handler;
    Channel channel;
    ASSERT_TRUE(channel.init(
        std::move(fds.read), ChannelEvents::readable | ChannelEvents::edge_triggered, handler
    ));
    ASSERT_TRUE(reactor.add(channel));

    reactor.remove(channel);
    write_byte(fds.write);
    reactor.run_once(10ms);

    EXPECT_EQ(handler.count, 0);
    EXPECT_FALSE(channel.registered());
}

} // namespace
