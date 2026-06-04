#include "ddcs/ctrl/infra/transport/acceptor.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/infra/transport/connection_coordinator.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/inbound.hpp"
#include "ddcs/proto/frame/frame.hpp"
#include "ddcs/runtime/reactor.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <cstdint>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using ddcs::common::Fd;
using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;
using ddcs::ctrl::infra::transport::Acceptor;
using ddcs::ctrl::infra::transport::ConnectionCoordinator;
using ddcs::ctrl::port::transport::CloseReason;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Inbound;
using ddcs::runtime::Reactor;
using namespace std::chrono_literals;

class MockInbound : public Inbound {
public:
    std::vector<ConnectionId> connected;
    std::vector<std::uint8_t> recv_type;
    std::vector<std::string> recv_body;

    void on_connect(ConnectionId id) override { connected.push_back(id); }
    void on_recv(ConnectionId, std::uint8_t type, PoolHandle<LinearBuffer> body) override {
        recv_type.push_back(type);
        auto const r = body->readable();
        recv_body.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }
    void on_close_request(ConnectionId, CloseReason) override {}
    void on_disconnect(ConnectionId) override {}
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

TEST(AcceptorTest, AcceptsConnectionAndDeliversFrame) {
    Reactor reactor;
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);
    Acceptor acceptor{reactor, coord, 0, 128}; // ephemeral port
    acceptor.start();

    std::uint16_t const port = acceptor.port();
    ASSERT_NE(port, 0);

    Fd client = connect_loopback(port);
    ASSERT_TRUE(client.valid());

    reactor.run_once(1000ms); // accept -> on_connect
    ASSERT_EQ(inbound.connected.size(), 1u);

    // 프레임 전송: magic | type=0x07 | len=2 | "ok"
    namespace frame = ddcs::proto::frame;
    auto const hb = frame::encode({.magic = frame::magic, .type = 0x07, .length = 2});
    char const body[] = "ok";
    ASSERT_EQ(::write(client.get(), hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    ASSERT_EQ(::write(client.get(), body, 2), 2);

    reactor.run_once(1000ms); // recv -> on_framing -> on_recv

    ASSERT_EQ(inbound.recv_type.size(), 1u);
    EXPECT_EQ(inbound.recv_type[0], 0x07);
    EXPECT_EQ(inbound.recv_body[0], "ok");
}
