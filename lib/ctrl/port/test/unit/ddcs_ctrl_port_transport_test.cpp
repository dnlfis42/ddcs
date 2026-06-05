#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/inbound.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;
using namespace ddcs::ctrl::port::transport;

// 인터페이스가 self-contained + implementable 한지 확인용 mock.
class MockInbound : public Inbound {
public:
    void on_connected(ConnectionId) override {}
    void on_recv(ConnectionId, std::uint8_t, PoolHandle<LinearBuffer>) override {}
    void on_disconnecting(ConnectionId, DisconnectReason) override {}
    void on_disconnected(ConnectionId) override {}
};

class MockOutbound : public Outbound {
public:
    PoolHandle<LinearBuffer> send_buffer() override { return {}; }
    void send(ConnectionId, std::uint8_t, PoolHandle<LinearBuffer>) override {}
    void drop(ConnectionId) override {}
};

} // namespace

TEST(PortTransportTest, InboundIsImplementable) {
    MockInbound mock;
    Inbound& port = mock;
    port.on_connected(ConnectionId{1});
    SUCCEED();
}

TEST(PortTransportTest, OutboundIsImplementable) {
    MockOutbound mock;
    Outbound& port = mock;
    port.drop(ConnectionId{1});
    SUCCEED();
}

TEST(PortTransportTest, ConnectionIdIsStrongAndDefaultInvalid) {
    ConnectionId const a{1};
    ConnectionId const b{1};
    ConnectionId const c{2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_TRUE(a.valid());
    EXPECT_FALSE(ConnectionId{}.valid());
}
