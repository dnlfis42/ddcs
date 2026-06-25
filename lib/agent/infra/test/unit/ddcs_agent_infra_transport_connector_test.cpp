#include "ddcs/agent/infra/transport/connector.hpp"

#include "ddcs/agent/app/transport/port/inbound.hpp"
#include "ddcs/agent/app/transport/port/outbound.hpp"
#include "ddcs/agent/app/transport/port/timer_slot.hpp"
#include "ddcs/agent/infra/transport/connection.hpp"
#include "ddcs/common/clock.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_scheduler.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

namespace frame = ddcs::wire::frame;

using ddcs::agent::app::transport::port::Inbound;
using ddcs::agent::app::transport::port::MessageBuffer;
using ddcs::agent::app::transport::port::Outbound;
using ddcs::agent::app::transport::port::TimerSlot;
using ddcs::agent::infra::transport::Connection;
using ddcs::agent::infra::transport::Connector;
using ddcs::common::ManualClock;
using ddcs::io::Fd;
using ddcs::io::Reactor;
using ddcs::io::TimerScheduler;
using namespace std::chrono_literals;

// app(SessionService) 대역
// - Inbound 통지를 기록하고, on_connected에서 옵션에 따라 등록 성공 통지나 close()를 흉내낸다.
class MockHandler : public Inbound {
public:
    int connected = 0;
    int disconnected = 0;
    std::vector<std::string> recvs; // 수신한 msg payload 통째 (`[type][body]`)
    std::vector<TimerSlot> timers;

    Outbound* out = nullptr; // &connector. close()/notify_registered() 재진입에 사용
    bool notify_registered_on_connected = false;
    bool close_on_connected = false;

    void on_connected() override {
        ++connected;
        if (notify_registered_on_connected && out != nullptr) {
            out->notify_registered();
        }
        if (close_on_connected && out != nullptr) {
            out->close();
        }
    }

    void on_recv(MessageBuffer payload) override {
        auto const r = payload->data_span();
        recvs.emplace_back(reinterpret_cast<char const*>(r.data()), r.size());
    }

    void on_disconnected() override {
        ++disconnected;
    }

    void on_timer(TimerSlot id) override {
        timers.push_back(id);
    }
};

// 127.0.0.1 ephemeral 포트로 listen하는 helper
// - Connector는 fd 주입 훅이 없어 실제 소켓에 붙어야 connected에 도달한다.
//   (Server::handle_accepted 같은 것이 없다.)
struct TcpListener {
    Fd fd;
    std::uint16_t port = 0;

    TcpListener() {
        int const s = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        EXPECT_GE(s, 0);
        int yes = 1;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // ephemeral
        EXPECT_EQ(::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        EXPECT_EQ(::listen(s, 16), 0);
        socklen_t len = sizeof(addr);
        EXPECT_EQ(::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len), 0);
        port = ::ntohs(addr.sin_port);
        fd = Fd{s};
    }

    // blocking accept: connect()는 loopback에서 곧 큐에 들어오므로 대기는 짧다.
    Fd accept_one() {
        int const c = ::accept(fd.get(), nullptr, nullptr);
        EXPECT_GE(c, 0);
        return Fd{c};
    }
};

// 서버(peer) 측 fd로 한 프레임을 써넣는다. (controller server 테스트의 write_frame과 동일 발상)
void write_frame(int fd, std::uint8_t type, std::string_view body) {
    std::string payload;
    payload.push_back(static_cast<char>(type));
    payload.append(body);
    auto const hb = frame::encode(static_cast<std::uint16_t>(payload.size()));
    ASSERT_EQ(::write(fd, hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    ASSERT_EQ(::write(fd, payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));
}

struct ConnectorFixture {
    Reactor reactor;
    ManualClock clock;
    TimerScheduler timers{reactor, clock}; // 주입 clock: 타이머는 dispatch_expired로 구동
    TcpListener listener;
    MockHandler handler;
    Connector connector{reactor, timers, std::string{"127.0.0.1"}, listener.port};

    ConnectorFixture() {
        handler.out = &connector;
        connector.init(handler);
    }

    // 소켓 I/O를 1회 디스패치
    // - connect 완료/프레임 수신 같은 epoll 이벤트는 항상 대기 중이라 즉시 반환한다.
    //   타이머는 여기서 만료되지 않는다. (dispatch_expired로 분리 구동)
    void pump() {
        reactor.run_once(200ms);
    }

    // 첫 connect를 개시하고 connected까지 구동(on_connected 발화). 서버측 peer fd 반환.
    Fd open() {
        connector.start();
        Fd peer = listener.accept_one();
        pump();
        return peer;
    }
};

TEST(AgentConnectorTest, ConnectReachesConnectedAndNotifiesHandler) {
    ConnectorFixture f;

    Fd const peer = f.open();

    EXPECT_EQ(f.connector.state(), Connection::State::connected);
    EXPECT_EQ(f.handler.connected, 1);
    EXPECT_EQ(f.handler.disconnected, 0);
}

TEST(AgentConnectorTest, GoodFrameDeliveredWholePayload) {
    ConnectorFixture f;
    Fd const peer = f.open();

    write_frame(peer.get(), 0x42, "abc");
    f.pump();

    ASSERT_EQ(f.handler.recvs.size(), 1u);
    EXPECT_EQ(
        f.handler.recvs[0], std::string(
                                "\x42"
                                "abc",
                                4
                            )
    );
    EXPECT_EQ(f.handler.disconnected, 0);
}

TEST(AgentConnectorTest, TypeOnlyFrameDelivered) {
    ConnectorFixture f;
    Fd const peer = f.open();

    write_frame(peer.get(), 0x10, "");
    f.pump();

    ASSERT_EQ(f.handler.recvs.size(), 1u);
    ASSERT_EQ(f.handler.recvs[0].size(), 1u); // type 1바이트만
    EXPECT_EQ(static_cast<std::uint8_t>(f.handler.recvs[0][0]), 0x10u);
}

TEST(AgentConnectorTest, MultipleFramesInOneBurstAllDelivered) {
    ConnectorFixture f;
    Fd const peer = f.open();

    write_frame(peer.get(), 0x01, "one");
    write_frame(peer.get(), 0x02, "two");
    f.pump();

    ASSERT_EQ(f.handler.recvs.size(), 2u);
    EXPECT_EQ(
        f.handler.recvs[0], std::string(
                                "\x01"
                                "one",
                                4
                            )
    );
    EXPECT_EQ(
        f.handler.recvs[1], std::string(
                                "\x02"
                                "two",
                                4
                            )
    );
}

TEST(AgentConnectorTest, PartialFrameWaitsThenDelivers) {
    ConnectorFixture f;
    Fd const peer = f.open();

    auto const hb = frame::encode(4);                // payload length = type(1) + body(3)
    ASSERT_EQ(::write(peer.get(), hb.data(), 2), 2); // header 절반만
    f.pump();
    EXPECT_TRUE(f.handler.recvs.empty());
    EXPECT_EQ(f.connector.state(), Connection::State::connected); // 부분 frame은 오류가 아님

    ASSERT_EQ(
        ::write(peer.get(), hb.data() + 2, hb.size() - 2), static_cast<ssize_t>(hb.size() - 2)
    );
    std::array<char, 4> const body{0x42, 'a', 'b', 'c'};
    ASSERT_EQ(::write(peer.get(), body.data(), body.size()), static_cast<ssize_t>(body.size()));
    f.pump();

    ASSERT_EQ(f.handler.recvs.size(), 1u);
    EXPECT_EQ(
        f.handler.recvs[0], std::string(
                                "\x42"
                                "abc",
                                4
                            )
    );
    EXPECT_EQ(f.handler.disconnected, 0);
}

TEST(AgentConnectorTest, RxBufferFullReReadDeliversAllFrames) {
    ConnectorFixture f;
    Fd const peer = f.open();
    ASSERT_EQ(f.connector.state(), Connection::State::connected);

    // rx ring = 4096
    // - (header 4 + payload 501) * 10 = 5050 바이트 > 4096 이므로
    //   단일 receive가 ring을 다 못 담아 IoResult::full -> framing -> continue 재독 경로를 태운다.
    constexpr std::size_t n = 10;
    std::string const body(500, 'x');
    for (std::size_t i = 0; i < n; ++i) {
        write_frame(peer.get(), static_cast<std::uint8_t>(0x20u + i), body);
    }
    f.pump();

    ASSERT_EQ(f.handler.recvs.size(), n);
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_EQ(f.handler.recvs[i].size(), body.size() + 1);
        EXPECT_EQ(
            static_cast<std::uint8_t>(f.handler.recvs[i][0]), static_cast<std::uint8_t>(0x20u + i)
        );
    }
    EXPECT_EQ(f.handler.disconnected, 0);
}

TEST(AgentConnectorTest, BadMagicDisconnectsAndGoesIdle) {
    ConnectorFixture f;
    Fd const peer = f.open();
    ASSERT_EQ(f.connector.state(), Connection::State::connected);

    std::array<std::uint8_t, 4> const junk{0xDE, 0xAD, 0x00, 0x00}; // magic != 0xDDC5
    ASSERT_EQ(::write(peer.get(), junk.data(), junk.size()), static_cast<ssize_t>(junk.size()));
    f.pump();

    EXPECT_TRUE(f.handler.recvs.empty());
    EXPECT_EQ(f.handler.disconnected, 1);
    EXPECT_EQ(f.connector.state(), Connection::State::idle);
}

TEST(AgentConnectorTest, OversizedFrameDisconnectsAndGoesIdle) {
    ConnectorFixture f;
    Fd const peer = f.open();
    ASSERT_EQ(f.connector.state(), Connection::State::connected);

    auto const hb = frame::encode(0xFFFF); // length 0xFFFF > max_payload_length(1024)
    ASSERT_EQ(::write(peer.get(), hb.data(), hb.size()), static_cast<ssize_t>(hb.size()));
    f.pump();

    EXPECT_TRUE(f.handler.recvs.empty());
    EXPECT_EQ(f.handler.disconnected, 1);
    EXPECT_EQ(f.connector.state(), Connection::State::idle);
}

TEST(AgentConnectorTest, PeerCloseDisconnectsThenReconnectTimerReconnects) {
    ConnectorFixture f;
    Fd peer = f.open();
    ASSERT_EQ(f.connector.state(), Connection::State::connected);

    peer.close(); // 서버측 FIN
    f.pump();

    EXPECT_EQ(f.handler.disconnected, 1);
    EXPECT_EQ(f.connector.state(), Connection::State::idle);

    // 끊김 시 reconnect 타이머가 무장된다. 만료시키면 다시 connect로 진입한다.
    f.clock.advance(60s); // 어떤 backoff(cap 30s)도 만료
    f.timers.dispatch_expired();
    ASSERT_EQ(f.connector.state(), Connection::State::connecting);

    Fd const peer2 = f.listener.accept_one();
    f.pump();
    EXPECT_EQ(f.connector.state(), Connection::State::connected);
    EXPECT_EQ(f.handler.connected, 2);
}

// 회귀 가드
// - 등록이 끝나지 않으면(매 연결 직후 close) backoff가 지수적으로 자라야 한다.
//   (jitter +/-25% 경계는 겹치지 않는다: attempt0 delay in [0.75,1.25]s, attempt1 in [1.5,2.5]s)
//   따라서 두 번째 reconnect 타이머가 1.3s 창에서 만료되지 않으면 backoff가 자란 것이다.
//   버그가 살아있으면  (reset이 TCP connect에서 일어나면) attempt가 0으로 묶여 1.3s 안에 만료된다.
TEST(AgentConnectorTest, BackoffGrowsWhileRegistrationNeverSucceeds) {
    ConnectorFixture f;
    f.handler.close_on_connected = true; // 등록 없이 매 연결 직후 close (등록 실패 사이클 모사)

    // cycle 1: connect -> on_connected -> close -> reconnect T1 무장 (arm 시점 attempt 0)
    {
        Fd const p1 = f.open();
    }
    ASSERT_EQ(f.connector.state(), Connection::State::idle);
    ASSERT_EQ(f.handler.disconnected, 1);

    // T1(base 범위, <=1.25s) 만료 -> 재연결 -> 다시 close -> reconnect T2 무장
    f.clock.advance(2s);
    f.timers.dispatch_expired();
    ASSERT_EQ(f.connector.state(), Connection::State::connecting);
    {
        Fd const p2 = f.listener.accept_one();
        f.pump();
    }
    ASSERT_EQ(f.connector.state(), Connection::State::idle);
    ASSERT_EQ(f.handler.disconnected, 2);

    // T2를 base 창(1.3s)으로 측정: backoff가 자랐다면(attempt1 >= 1.5s) 만료되지 않는다.
    f.clock.advance(1300ms);
    f.timers.dispatch_expired();
    EXPECT_EQ(f.connector.state(), Connection::State::idle)
        << "reconnect fired within the base window; exponential backoff did not grow "
           "(backoff reset is happening on TCP connect instead of on registration success)";

    // 충분히 advance하면 결국 만료(자란 backoff도 cap 30s 이내)되어 재연결한다.
    f.clock.advance(60s);
    f.timers.dispatch_expired();
    EXPECT_EQ(f.connector.state(), Connection::State::connecting);
}

// 등록 성공(notify_registered)이 매 연결마다 통지되면
// backoff는 base로 리셋되어 두 번째 reconnect 타이머도 base 창(1.3s)에서 만료된다.
TEST(AgentConnectorTest, NotifyRegisteredKeepsBackoffAtBase) {
    ConnectorFixture f;
    f.handler.notify_registered_on_connected = true; // 매 연결마다 등록 성공 통지
    f.handler.close_on_connected = true;             // 그 후 close

    {
        Fd const p1 = f.open();
    }
    ASSERT_EQ(f.connector.state(), Connection::State::idle);

    f.clock.advance(2s);
    f.timers.dispatch_expired();
    ASSERT_EQ(f.connector.state(), Connection::State::connecting);
    {
        Fd const p2 = f.listener.accept_one();
        f.pump();
    }
    ASSERT_EQ(f.connector.state(), Connection::State::idle);

    // notify_registered가 매번 backoff를 리셋했으므로 T2도 base 범위 -> 1.3s 안에 만료된다.
    f.clock.advance(1300ms);
    f.timers.dispatch_expired();
    EXPECT_EQ(f.connector.state(), Connection::State::connecting)
        << "notify_registered should have reset backoff to base";
}

} // namespace
