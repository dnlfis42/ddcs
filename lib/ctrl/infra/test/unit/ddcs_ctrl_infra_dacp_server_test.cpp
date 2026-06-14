#include "ddcs/ctrl/infra/dacp/server.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/connection_observer.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/disconnector.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"
#include "ddcs/ctrl/app/agent/port/message_sender.hpp"
#include "ddcs/ctrl/infra/dacp/peer_address.hpp"
#include "ddcs/dacp/frame/frame.hpp"
#include "ddcs/io/reactor.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

namespace frame = ddcs::dacp::frame;

using ddcs::common::Fd;
using ddcs::ctrl::app::agent::port::ConnectionId;
using ddcs::ctrl::app::agent::port::ConnectionObserver;
using ddcs::ctrl::app::agent::port::DisconnectReason;
using ddcs::ctrl::app::agent::port::MessageBuffer;
using ddcs::ctrl::infra::dacp::PeerAddress;
using ddcs::ctrl::infra::dacp::Server;
using ddcs::io::Reactor;
using namespace std::chrono_literals;

// 이벤트 기록용 mock observer (app 측 대역).
class MockObserver : public ConnectionObserver {
public:
    std::vector<ConnectionId> connected;
    std::vector<std::pair<ConnectionId, DisconnectReason>> disconnected;
    std::vector<std::uint8_t> message_type;
    std::vector<std::string> message_body;

    void on_connected(ConnectionId id) override { connected.push_back(id); }
    void on_message(ConnectionId, std::uint8_t type, MessageBuffer body) override {
        message_type.push_back(type);
        auto const r = body->readable();
        message_body.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }
    void on_disconnected(ConnectionId id, DisconnectReason reason) override { disconnected.emplace_back(id, reason); }
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

void write_frame(int fd, std::uint8_t type, std::string_view body) {
    auto const hb =
        frame::encode({.magic = frame::magic, .type = type, .length = static_cast<std::uint16_t>(body.size())});
    ASSERT_EQ(::write(fd, hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    if (!body.empty()) {
        ASSERT_EQ(::write(fd, body.data(), body.size()), static_cast<ssize_t>(body.size()));
    }
}

// reactor + server + mock observer 조립. accept는 handle_accepted로 직접 주입.
// observer는 server보다 먼저 선언: server dtor가 notify하므로 server가 먼저 파괴돼야 한다.
struct ServerFixture {
    Reactor reactor;
    MockObserver observer;
    Server server{reactor, 0, 8};

    ServerFixture() {
        EXPECT_TRUE(server.init(observer));
        EXPECT_TRUE(server.start()); // 미시작 상태로 connection을 주입하면 dtor가 reap하지 않는다
    }

    ConnectionId accept(SocketPair& sp) {
        server.handle_accepted(sp.take_conn(), PeerAddress{});
        EXPECT_FALSE(observer.connected.empty());
        return observer.connected.back();
    }
};

} // namespace

TEST(DacpServerTest, AcceptNotifiesConnected) {
    ServerFixture f;
    SocketPair sp;

    ConnectionId const id = f.accept(sp);

    EXPECT_TRUE(id.valid());
    EXPECT_EQ(f.observer.connected.size(), 1u);
}

TEST(DacpServerTest, FramedMessageDeliveredToOnMessage) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    write_frame(sp.peer, 0x42, "abc");
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.message_type.size(), 1u);
    EXPECT_EQ(f.observer.message_type[0], 0x42);
    EXPECT_EQ(f.observer.message_body[0], "abc");
}

TEST(DacpServerTest, EmptyBodyFrameDelivered) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    write_frame(sp.peer, 0x10, "");
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.message_type.size(), 1u);
    EXPECT_EQ(f.observer.message_type[0], 0x10);
    EXPECT_TRUE(f.observer.message_body[0].empty());
}

TEST(DacpServerTest, PartialFrameWaitsForCompletion) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    auto const hb = frame::encode({.magic = frame::magic, .type = 0x42, .length = 3});
    ASSERT_EQ(::write(sp.peer, hb.data(), 2), 2); // header 일부만
    f.reactor.run_once(1000ms);
    EXPECT_TRUE(f.observer.message_type.empty());

    ASSERT_EQ(::write(sp.peer, hb.data() + 2, hb.size() - 2), static_cast<ssize_t>(hb.size() - 2));
    ASSERT_EQ(::write(sp.peer, "abc", 3), 3);
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.message_type.size(), 1u);
    EXPECT_EQ(f.observer.message_body[0], "abc");
    EXPECT_TRUE(f.observer.disconnected.empty()); // 부분 frame은 오류가 아님
}

TEST(DacpServerTest, MultipleFramesInOneBurstAllDelivered) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    write_frame(sp.peer, 0x01, "one");
    write_frame(sp.peer, 0x02, "two");
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.message_type.size(), 2u);
    EXPECT_EQ(f.observer.message_type[0], 0x01);
    EXPECT_EQ(f.observer.message_body[0], "one");
    EXPECT_EQ(f.observer.message_type[1], 0x02);
    EXPECT_EQ(f.observer.message_body[1], "two");
}

TEST(DacpServerTest, BadMagicDisconnectsWithDacpError) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    std::array<std::uint8_t, 5> const junk{0xDE, 0xAD, 0x42, 0x00, 0x00};
    ASSERT_EQ(::write(sp.peer, junk.data(), junk.size()), static_cast<ssize_t>(junk.size()));
    f.reactor.run_once(1000ms);

    EXPECT_TRUE(f.observer.message_type.empty());
    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].first, id);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::dacp_error);
}

TEST(DacpServerTest, OversizedLengthDisconnectsWithDacpError) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    // length=0xFFFF > max_length(128): protocol 한계 초과.
    auto const hb = frame::encode({.magic = frame::magic, .type = 0x42, .length = 0xFFFF});
    ASSERT_EQ(::write(sp.peer, hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::dacp_error);
}

TEST(DacpServerTest, SendFramesMessageOnWire) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    auto buf = f.server.sender().make_message_buffer();
    char const body[] = "hi";
    ASSERT_TRUE(buf->write({reinterpret_cast<std::byte const*>(body), 2}));
    f.server.sender().send(id, 0x11, std::move(buf));
    f.reactor.run_once(1000ms); // writable -> transmit

    std::array<std::byte, 16> got{};
    ASSERT_EQ(::read(sp.peer, got.data(), got.size()), static_cast<ssize_t>(frame::header_size + 2));
    frame::HeaderBytes hb{};
    std::memcpy(hb.data(), got.data(), frame::header_size);
    auto const header = frame::parse(hb);
    ASSERT_TRUE(header);
    EXPECT_EQ(header->type, std::uint8_t{0x11});
    EXPECT_EQ(header->length, std::uint16_t{2});
    EXPECT_EQ(std::memcmp(got.data() + frame::header_size, "hi", 2), 0);
}

TEST(DacpServerTest, PeerFinDisconnectsAfterDeliveringPendingFrames) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    write_frame(sp.peer, 0x42, "last");
    ::close(sp.peer); // 데이터 직후 FIN
    sp.peer = -1;
    f.reactor.run_once(1000ms);

    // FIN 처리 전에 도착분이 먼저 전달된다.
    ASSERT_EQ(f.observer.message_type.size(), 1u);
    EXPECT_EQ(f.observer.message_body[0], "last");
    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].first, id);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::peer_closed);
}

TEST(DacpServerTest, DisconnectReapsAndNotifies) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    f.server.disconnector().disconnect(id);

    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].first, id);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::local_drop);
}

TEST(DacpServerTest, DisconnectInsideOnMessageIsSafe) {
    // on_message 재진입: 콜백 안에서 disconnect해도 dispatch 루프가 안전해야 한다.
    class DisconnectingObserver : public MockObserver {
    public:
        Server* server{nullptr};
        void on_message(ConnectionId id, std::uint8_t type, MessageBuffer body) override {
            MockObserver::on_message(id, type, std::move(body));
            server->disconnector().disconnect(id);
        }
    };

    Reactor reactor;
    DisconnectingObserver observer;
    Server server{reactor, 0, 8}; // observer보다 늦게 생성 - dtor에서 notify하므로 먼저 파괴
    observer.server = &server;
    ASSERT_TRUE(server.init(observer));
    ASSERT_TRUE(server.start());

    SocketPair sp;
    server.handle_accepted(sp.take_conn(), PeerAddress{});
    ConnectionId const id = observer.connected.at(0);

    write_frame(sp.peer, 0x01, "one");
    write_frame(sp.peer, 0x02, "two"); // 첫 frame 처리 중 끊기므로 전달되지 않아야 함
    reactor.run_once(1000ms);

    ASSERT_EQ(observer.message_type.size(), 1u);
    EXPECT_EQ(observer.message_body[0], "one");
    ASSERT_EQ(observer.disconnected.size(), 1u);
    EXPECT_EQ(observer.disconnected[0].first, id);
}
