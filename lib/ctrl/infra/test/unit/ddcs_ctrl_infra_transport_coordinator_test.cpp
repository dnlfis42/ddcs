#include "ddcs/ctrl/infra/transport/connection_coordinator.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/infra/transport/endpoint.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/ctrl/port/transport/inbound.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"
#include "ddcs/proto/frame/frame.hpp"
#include "ddcs/runtime/reactor.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>
#include <vector>

#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using ddcs::common::Fd;
using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;
using ddcs::ctrl::infra::transport::ConnectionCoordinator;
using ddcs::ctrl::infra::transport::Endpoint;
using ddcs::ctrl::port::transport::CloseMode;
using ddcs::ctrl::port::transport::CloseReason;
using ddcs::ctrl::port::transport::ConnectionId;
using ddcs::ctrl::port::transport::Inbound;
using ddcs::runtime::Reactor;
using namespace std::chrono_literals;

// 이벤트 기록용 mock Inbound (app 측 대역).
class MockInbound : public Inbound {
public:
    std::vector<ConnectionId> connected;
    std::vector<ConnectionId> disconnected;
    std::vector<ConnectionId> close_requested;
    std::vector<std::uint8_t> recv_type;
    std::vector<std::string> recv_body;

    void on_connect(ConnectionId id) override { connected.push_back(id); }
    void on_recv(ConnectionId, std::uint8_t type, PoolHandle<LinearBuffer> body) override {
        recv_type.push_back(type);
        auto const r = body->readable();
        recv_body.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }
    void on_close_request(ConnectionId id, CloseReason) override { close_requested.push_back(id); }
    void on_disconnect(ConnectionId id) override { disconnected.push_back(id); }
};

// socketpair 한 쌍: conn 측 fd 를 nonblocking 으로 넘기고, peer 측은 보관.
struct SocketPair {
    int peer{-1};
    Fd take_conn() {
        int fds[2];
        EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        int const fl = ::fcntl(fds[0], F_GETFL, 0);
        EXPECT_EQ(::fcntl(fds[0], F_SETFL, fl | O_NONBLOCK), 0);
        peer = fds[1];
        return Fd{fds[0]};
    }
    ~SocketPair() {
        if (peer >= 0) {
            ::close(peer);
        }
    }
};

} // namespace

TEST(ConnectionCoordinatorTest, AcceptRegistersAndNotifiesConnect) {
    Reactor reactor;
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{});

    EXPECT_EQ(coord.size(), 1u);
    ASSERT_EQ(inbound.connected.size(), 1u);
    EXPECT_TRUE(inbound.connected[0].valid());
}

TEST(ConnectionCoordinatorTest, CloseForceReapsAndNotifiesDisconnect) {
    Reactor reactor;
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{});
    ConnectionId const id = inbound.connected.at(0);

    coord.close(id, CloseMode::force);
    EXPECT_EQ(coord.size(), 1u); // 아직 reap 전 (pending - 엔트리포인트에서 정리)

    // reap 은 coordinator 엔트리포인트 끝에서 일어난다. 아무 이벤트나 들어오면 구동됨.
    ::close(sp.peer); // conn fd readable -> on_event(closing) -> 끝에서 reap_closed
    sp.peer = -1;
    reactor.run_once(1000ms);

    EXPECT_EQ(coord.size(), 0u);
    ASSERT_EQ(inbound.disconnected.size(), 1u);
    EXPECT_EQ(inbound.disconnected[0], id);
}

TEST(ConnectionCoordinatorTest, PeerFinTriggersCloseRequest) {
    Reactor reactor;
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{});
    ConnectionId const id = inbound.connected.at(0);

    ::close(sp.peer); // FIN
    sp.peer = -1;

    reactor.run_once(1000ms); // EPOLLIN(FIN) -> on_readable -> peer_closed -> on_close_request

    ASSERT_EQ(inbound.close_requested.size(), 1u);
    EXPECT_EQ(inbound.close_requested[0], id);
}

TEST(ConnectionCoordinatorTest, FramedMessageDeliveredToOnRecv) {
    Reactor reactor;
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{});

    // 프레임 한 개: magic | type=0x42 | len=3 | "abc"
    namespace frame = ddcs::proto::frame;
    auto const hb = frame::encode({.magic = frame::magic, .type = 0x42, .length = 3});
    char const body[] = "abc";
    ASSERT_EQ(::write(sp.peer, hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    ASSERT_EQ(::write(sp.peer, body, 3), 3);

    reactor.run_once(1000ms); // EPOLLIN -> on_readable -> on_framing -> on_recv

    ASSERT_EQ(inbound.recv_type.size(), 1u);
    EXPECT_EQ(inbound.recv_type[0], 0x42);
    EXPECT_EQ(inbound.recv_body[0], "abc");
}

TEST(ConnectionCoordinatorTest, SendFramesBodyAndTransmits) {
    Reactor reactor;
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{});
    ConnectionId const id = inbound.connected.at(0);

    // headroom 예약된 버퍼에 "hi" 채워 send.
    auto buf = coord.payload_buffer();
    char const body[] = "hi";
    ASSERT_TRUE(buf->write({reinterpret_cast<std::byte const*>(body), 2}));
    coord.send(id, 0x11, std::move(buf));

    reactor.run_once(1000ms); // EPOLLOUT(무장) -> on_writable -> transmit

    // peer 가 frame 수신: magic | type=0x11 | len=2 | "hi" = 7 bytes
    namespace frame = ddcs::proto::frame;
    std::array<std::byte, 16> got{};
    ASSERT_EQ(::read(sp.peer, got.data(), got.size()), static_cast<ssize_t>(frame::header_size + 2));
    frame::HeaderBytes hb{};
    std::memcpy(hb.data(), got.data(), frame::header_size);
    auto const h = frame::decode(hb);
    EXPECT_EQ(h.magic, frame::magic);
    EXPECT_EQ(h.type, std::uint8_t{0x11});
    EXPECT_EQ(h.length, std::uint16_t{2});
    EXPECT_EQ(std::memcmp(got.data() + frame::header_size, "hi", 2), 0);
}

TEST(ConnectionCoordinatorTest, GracefulCloseHalfClosesThenReapsOnPeerFin) {
    Reactor reactor;
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{});
    ConnectionId const id = inbound.connected.at(0);

    coord.close(id, CloseMode::graceful); // tx empty -> begin_passive_wait -> shutdown(WR)

    // peer 가 우리 쪽 FIN(half-close) 을 받는다: read -> 0
    char rbuf[8];
    EXPECT_EQ(::read(sp.peer, rbuf, sizeof(rbuf)), 0);

    // peer 도 FIN -> conn passive_wait 에서 peer FIN 완주 -> closing -> reap_closed
    ::close(sp.peer);
    sp.peer = -1;
    reactor.run_once(1000ms);

    EXPECT_EQ(coord.size(), 0u);
    ASSERT_EQ(inbound.disconnected.size(), 1u);
    EXPECT_EQ(inbound.disconnected[0], id);
}

TEST(ConnectionCoordinatorTest, HandshakeTimeoutClosesSilentConnection) {
    ddcs::common::ManualClock clk;
    Reactor reactor{clk};
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{}); // 연결만, 프레임 안 보냄
    EXPECT_EQ(coord.size(), 1u);

    clk.advance(std::chrono::seconds{4}); // > 3s handshake 한도
    reactor.run_once(0ms);                // handshake 발화 -> force close -> reap

    EXPECT_EQ(coord.size(), 0u);
    ASSERT_EQ(inbound.disconnected.size(), 1u);
}

TEST(ConnectionCoordinatorTest, FirstFrameCancelsHandshakeTimeout) {
    ddcs::common::ManualClock clk;
    Reactor reactor{clk};
    ConnectionCoordinator coord{reactor};
    MockInbound inbound;
    coord.init(inbound);

    SocketPair sp;
    coord.on_accept(sp.take_conn(), Endpoint{});

    // 첫 프레임(header-only) 전송 -> on_recv -> handshake 타이머 cancel.
    namespace frame = ddcs::proto::frame;
    auto const hb = frame::encode({.magic = frame::magic, .type = 0x42, .length = 0});
    ASSERT_EQ(::write(sp.peer, hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    reactor.run_once(0ms);
    ASSERT_EQ(inbound.recv_type.size(), 1u);

    clk.advance(std::chrono::seconds{10}); // handshake 한도 지나도
    reactor.run_once(0ms);                 // 취소됐으니 발화 안 함

    EXPECT_EQ(coord.size(), 1u); // 연결 유지
    EXPECT_TRUE(inbound.disconnected.empty());
}
