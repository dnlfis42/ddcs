#include "ddcs/ctrl/infra/transport/connection_acceptor.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/infra/transport/connection_manager.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/inbound.hpp"
#include "ddcs/proto/frame/frame.hpp"
#include "ddcs/runtime/reactor.hpp"
#include "ddcs/runtime/timer_scheduler.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::common::Fd;
using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;
using ddcs::ctrl::infra::transport::ConnectionAcceptor;
using ddcs::ctrl::infra::transport::ConnectionManager;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::DisconnectReason;
using ddcs::ctrl::port::transport::Inbound;
using ddcs::runtime::Reactor;
using ddcs::runtime::TimerScheduler;
using namespace std::chrono_literals;

class MockInbound : public Inbound {
public:
    std::vector<ConnectionId> connected;
    std::vector<std::uint8_t> recv_type;
    std::vector<std::string> recv_body;

    void on_connected(ConnectionId id) override { connected.push_back(id); }
    void on_recv(ConnectionId, std::uint8_t type, PoolHandle<LinearBuffer> body) override {
        recv_type.push_back(type);
        auto const r = body->readable();
        recv_body.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }
    void on_disconnecting(ConnectionId, DisconnectReason) override {}
    void on_disconnected(ConnectionId) override {}
};

// 127.0.0.1:port 로 연결한 클라이언트 소켓.
Fd connect_loopback(std::uint16_t port) {
    Fd client{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(client.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        return Fd{};
    }
    return client;
}

} // namespace

TEST(ConnectionAcceptorTest, AcceptsConnectionAndDeliversFrame) {
    Reactor reactor;
    TimerScheduler timers{reactor};
    timers.start();
    ConnectionManager manager{reactor, timers};
    MockInbound inbound;
    manager.init(inbound);
    ConnectionAcceptor acceptor{reactor, manager, 0, 128}; // ephemeral port
    acceptor.start();

    std::uint16_t const port = acceptor.port();
    ASSERT_NE(port, 0);

    Fd client = connect_loopback(port);
    ASSERT_TRUE(client.valid());

    reactor.run_once(1000ms); // accept -> on_connected
    ASSERT_EQ(inbound.connected.size(), 1u);

    // 프레임 전송: magic | type=0x07 | payload_size=2 | "ok"
    namespace frame = ddcs::proto::frame;
    auto const hb = frame::encode({.magic = frame::magic, .type = 0x07, .payload_size = 2});
    char const body[] = "ok";
    ASSERT_EQ(::write(client.get(), hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    ASSERT_EQ(::write(client.get(), body, 2), 2);

    reactor.run_once(1000ms); // recv -> on_framing -> on_recv

    ASSERT_EQ(inbound.recv_type.size(), 1u);
    EXPECT_EQ(inbound.recv_type[0], 0x07);
    EXPECT_EQ(inbound.recv_body[0], "ok");
}
