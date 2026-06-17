#include "ddcs/ctrl/infra/frame/server.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/connection_observer.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/disconnector.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"
#include "ddcs/ctrl/app/agent/port/message_sender.hpp"
#include "ddcs/ctrl/infra/frame/peer_address.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

namespace frame = ddcs::wire::frame;

using ddcs::common::Fd;
using ddcs::ctrl::app::agent::port::ConnectionId;
using ddcs::ctrl::app::agent::port::ConnectionObserver;
using ddcs::ctrl::app::agent::port::DisconnectReason;
using ddcs::ctrl::app::agent::port::MessageBuffer;
using ddcs::ctrl::infra::frame::PeerAddress;
using ddcs::ctrl::infra::frame::Server;
using ddcs::io::Reactor;
using namespace std::chrono_literals;

// infra는 acmp를 모른다. payload(`[type][body]`)는 불투명 바이트로 다룬다.
constexpr std::size_t test_max_payload_size{256};

// 이벤트 기록용 mock observer (app 측 대역). payload를 통째로 보관한다.
class MockObserver : public ConnectionObserver {
public:
    std::vector<ConnectionId> connected;
    std::vector<std::pair<ConnectionId, DisconnectReason>> disconnected;
    std::vector<std::string> payload; // 수신한 acmp payload 통째 (`[type][body]`)

    void on_connected(ConnectionId id) override {
        connected.push_back(id);
    }
    void on_message(ConnectionId, MessageBuffer p) override {
        auto const r = p->data_span();
        payload.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }
    void on_disconnected(ConnectionId id, DisconnectReason reason) override {
        disconnected.emplace_back(id, reason);
    }
};

// socketpair 한 쌍: conn 측 fd를 nonblocking으로 넘기고, peer 측은 보관
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

// payload(= acmp `[type][body]`)를 frame으로 감싸 보낸다.
void write_frame(int fd, std::uint8_t type, std::string_view body) {
    std::string payload;
    payload.push_back(static_cast<char>(type));
    payload.append(body);
    auto const hb = frame::encode(static_cast<std::uint16_t>(payload.size()));
    ASSERT_EQ(::write(fd, hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    ASSERT_EQ(::write(fd, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));
}

// reactor + server + mock observer 조립. accept는 handle_accepted로 직접 주입
// observer는 server보다 먼저 선언: server dtor가 notify하므로 server가 먼저 파괴돼야 한다.
struct ServerFixture {
    Reactor reactor;
    MockObserver observer;
    Server server{reactor, 0, 8, test_max_payload_size};

    ServerFixture() {
        EXPECT_TRUE(server.init(observer));
        EXPECT_TRUE(server.start()); // 미시작 상태로 connection을 주입하면 dtor가 reap하지 않는다.
    }

    ConnectionId accept(SocketPair& sp) {
        server.handle_accepted(sp.take_conn(), PeerAddress{});
        EXPECT_FALSE(observer.connected.empty());
        return observer.connected.back();
    }
};

} // namespace

TEST(FrameServerTest, AcceptNotifiesConnected) {
    ServerFixture f;
    SocketPair sp;

    ConnectionId const id = f.accept(sp);

    EXPECT_TRUE(id.valid());
    EXPECT_EQ(f.observer.connected.size(), 1u);
}

TEST(FrameServerTest, FramedPayloadDeliveredToOnMessage) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    write_frame(sp.peer, 0x42, "abc");
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.payload.size(), 1u);
    // `[type][body]`
    EXPECT_EQ(
        f.observer.payload[0], std::string(
                                   "\x42"
                                   "abc",
                                   4
                               )
    );
}

TEST(FrameServerTest, TypeOnlyPayloadDelivered) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    write_frame(sp.peer, 0x10, "");
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.payload.size(), 1u);
    ASSERT_EQ(f.observer.payload[0].size(), 1u); // type 1바이트만
    EXPECT_EQ(static_cast<std::uint8_t>(f.observer.payload[0][0]), 0x10u);
}

TEST(FrameServerTest, PartialFrameWaitsForCompletion) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    auto const hb = frame::encode(4);             // payload length = type(1) + body(3)
    ASSERT_EQ(::write(sp.peer, hb.data(), 2), 2); // header 일부만
    f.reactor.run_once(1000ms);
    EXPECT_TRUE(f.observer.payload.empty());

    ASSERT_EQ(::write(sp.peer, hb.data() + 2, hb.size() - 2), static_cast<ssize_t>(hb.size() - 2));
    std::array<char, 4> const body{0x42, 'a', 'b', 'c'};
    ASSERT_EQ(::write(sp.peer, body.data(), body.size()), static_cast<ssize_t>(body.size()));
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.payload.size(), 1u);
    EXPECT_EQ(
        f.observer.payload[0], std::string(
                                   "\x42"
                                   "abc",
                                   4
                               )
    );
    EXPECT_TRUE(f.observer.disconnected.empty()); // 부분 frame은 오류가 아님
}

TEST(FrameServerTest, MultipleFramesInOneBurstAllDelivered) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    write_frame(sp.peer, 0x01, "one");
    write_frame(sp.peer, 0x02, "two");
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.payload.size(), 2u);
    EXPECT_EQ(
        f.observer.payload[0], std::string(
                                   "\x01"
                                   "one",
                                   4
                               )
    );
    EXPECT_EQ(
        f.observer.payload[1], std::string(
                                   "\x02"
                                   "two",
                                   4
                               )
    );
}

TEST(FrameServerTest, BadMagicDisconnectsWithProtocolError) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    // magic 2바이트가 0xDDC5가 아니라 parse 실패
    std::array<std::uint8_t, 4> const junk{0xDE, 0xAD, 0x00, 0x00};
    ASSERT_EQ(::write(sp.peer, junk.data(), junk.size()), static_cast<ssize_t>(junk.size()));
    f.reactor.run_once(1000ms);

    EXPECT_TRUE(f.observer.payload.empty());
    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].first, id);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::protocol_error);
}

TEST(FrameServerTest, OversizedLengthDisconnectsWithProtocolError) {
    ServerFixture f;
    SocketPair sp;
    f.accept(sp);

    // length=0xFFFF > max_payload_size: 용량 초과
    auto const hb = frame::encode(0xFFFF);
    ASSERT_EQ(::write(sp.peer, hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    f.reactor.run_once(1000ms);

    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::protocol_error);
}

TEST(FrameServerTest, SendFramesPayloadOnWire) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    // app은 acmp payload(`[type][body]`)를 통째로 넣는다. infra는 frame header만 덧씌운다.
    auto buf = f.server.sender().make_message_buffer();
    std::array<std::byte, 3> const payload{std::byte{0x11}, std::byte{'h'}, std::byte{'i'}};
    ASSERT_TRUE(buf->try_append(payload));
    f.server.sender().send(id, std::move(buf));
    f.reactor.run_once(1000ms); // writable이면 transmit

    std::array<std::byte, 16> got{};
    ASSERT_EQ(
        ::read(sp.peer, got.data(), got.size()), static_cast<ssize_t>(frame::header_size + 3)
    );
    frame::HeaderBytes hb{};
    std::memcpy(hb.data(), got.data(), frame::header_size);
    auto const header = frame::parse(hb);
    ASSERT_TRUE(header);
    EXPECT_EQ(header->payload_length, std::uint16_t{3});
    EXPECT_EQ(std::memcmp(got.data() + frame::header_size, payload.data(), 3), 0);
}

TEST(FrameServerTest, PeerFinDisconnectsAfterDeliveringPendingFrames) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    write_frame(sp.peer, 0x42, "last");
    ::close(sp.peer); // 데이터 직후 FIN
    sp.peer = -1;
    f.reactor.run_once(1000ms);

    // FIN 처리 전에 도착분이 먼저 전달된다.
    ASSERT_EQ(f.observer.payload.size(), 1u);
    EXPECT_EQ(
        f.observer.payload[0], std::string(
                                   "\x42"
                                   "last",
                                   5
                               )
    );
    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].first, id);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::peer_closed);
}

TEST(FrameServerTest, DisconnectReapsAndNotifies) {
    ServerFixture f;
    SocketPair sp;
    ConnectionId const id = f.accept(sp);

    f.server.disconnector().disconnect(id);

    ASSERT_EQ(f.observer.disconnected.size(), 1u);
    EXPECT_EQ(f.observer.disconnected[0].first, id);
    EXPECT_EQ(f.observer.disconnected[0].second, DisconnectReason::local_drop);
}

TEST(FrameServerTest, DisconnectInsideOnMessageIsSafe) {
    // on_message 재진입: 콜백 안에서 disconnect해도 dispatch 루프가 안전해야 한다.
    class DisconnectingObserver : public MockObserver {
    public:
        Server* server{nullptr};
        void on_message(ConnectionId id, MessageBuffer p) override {
            MockObserver::on_message(id, std::move(p));
            server->disconnector().disconnect(id);
        }
    };

    Reactor reactor;
    DisconnectingObserver observer;
    Server server{
        reactor, 0, 8, test_max_payload_size
    }; // observer보다 늦게 생성 (dtor에서 notify하므로 먼저 파괴)
    observer.server = &server;
    ASSERT_TRUE(server.init(observer));
    ASSERT_TRUE(server.start());

    SocketPair sp;
    server.handle_accepted(sp.take_conn(), PeerAddress{});
    ConnectionId const id = observer.connected.at(0);

    write_frame(sp.peer, 0x01, "one");
    write_frame(sp.peer, 0x02, "two"); // 첫 frame 처리 중 끊기므로 전달되지 않아야 함
    reactor.run_once(1000ms);

    ASSERT_EQ(observer.payload.size(), 1u);
    EXPECT_EQ(
        observer.payload[0], std::string(
                                 "\x01"
                                 "one",
                                 4
                             )
    );
    ASSERT_EQ(observer.disconnected.size(), 1u);
    EXPECT_EQ(observer.disconnected[0].first, id);
}
