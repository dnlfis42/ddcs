#include "ddcs/io/channel_events.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::io::ChannelEvents;
using ddcs::io::contains;
using ddcs::io::to_underlying;

TEST(ChannelEventsTest, CombinesBitsWithOr) {
    auto const events = ChannelEvents::readable | ChannelEvents::writable;

    EXPECT_TRUE(contains(events, ChannelEvents::readable));
    EXPECT_TRUE(contains(events, ChannelEvents::writable));
    EXPECT_FALSE(contains(events, ChannelEvents::error));
}

TEST(ChannelEventsTest, AddsBitsWithOrAssign) {
    ChannelEvents events{ChannelEvents::readable};

    events |= ChannelEvents::edge_triggered;

    EXPECT_TRUE(contains(events, ChannelEvents::readable));
    EXPECT_TRUE(contains(events, ChannelEvents::edge_triggered));
}

TEST(ChannelEventsTest, ReturnsCommonBitsWithAnd) {
    auto const events = ChannelEvents::readable | ChannelEvents::writable;
    auto const common = events & (ChannelEvents::writable | ChannelEvents::error);

    EXPECT_EQ(common, ChannelEvents::writable);
}

TEST(ChannelEventsTest, RequiresAllBitsForContains) {
    auto const events = ChannelEvents::readable | ChannelEvents::writable;

    EXPECT_TRUE(contains(events, ChannelEvents::readable | ChannelEvents::writable));
    EXPECT_FALSE(contains(events, ChannelEvents::readable | ChannelEvents::error));
    EXPECT_TRUE(contains(events, ChannelEvents::none));
}

TEST(ChannelEventsTest, ReturnsMaskValueWithToUnderlying) {
    EXPECT_EQ(to_underlying(ChannelEvents::readable), 1u << 0);
    EXPECT_EQ(to_underlying(ChannelEvents::edge_triggered), 1u << 16);
}

} // namespace
