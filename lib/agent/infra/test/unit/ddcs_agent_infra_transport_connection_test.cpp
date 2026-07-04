#include "ddcs/agent/infra/transport/connection.hpp"

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/fd.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::agent::infra::transport::Connection;
using ddcs::common::LinearBuffer;
using ddcs::io::Fd;

// socketpair 한쪽을 connected Connection으로 만든다.
void make_connected(Connection& conn, int fd) {
    ASSERT_TRUE(conn.assign(Fd{fd}, ddcs::io::ChannelEvents::none));
    ASSERT_TRUE(conn.transition(Connection::State::connecting));
    ASSERT_TRUE(conn.transition(Connection::State::connected));
}

TEST(AgentConnectionTest, ReceiveFillsRxBuffer) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
    Connection conn;
    make_connected(conn, sv[0]);
    Fd peer{sv[1]};

    char const msg[] = "hello";
    ASSERT_EQ(::write(peer.get(), msg, 5), 5);

    EXPECT_EQ(conn.receive(), Connection::IoResult::would_block);
    EXPECT_EQ(conn.rx_buffer().size(), 5u);
    std::array<std::byte, 5> buf{};
    EXPECT_TRUE(conn.rx_buffer().try_read({buf.data(), buf.size()}));
    EXPECT_EQ(std::memcmp(buf.data(), msg, 5), 0);
}

TEST(AgentConnectionTest, TransmitDrainsTxQueue) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
    Connection conn;
    make_connected(conn, sv[0]);
    Fd peer{sv[1]};

    auto pool = ddcs::common::ObjectPool<LinearBuffer>::create<4>(std::size_t{64});
    auto out = pool.acquire();
    char const msg[] = "world";
    ASSERT_TRUE(out->try_append({reinterpret_cast<std::byte const*>(msg), 5}));
    conn.tx_enqueue(std::move(out));

    EXPECT_EQ(conn.transmit(), Connection::IoResult::ok);
    EXPECT_TRUE(conn.tx_empty());

    std::array<char, 8> rcv{};
    EXPECT_EQ(::read(peer.get(), rcv.data(), rcv.size()), 5);
    EXPECT_EQ(std::memcmp(rcv.data(), msg, 5), 0);
}

TEST(AgentConnectionTest, ReceiveReportsPeerClosed) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
    Connection conn;
    make_connected(conn, sv[0]);
    {
        Fd const peer{sv[1]};
    } // 스코프 종료 후 close되어 FIN

    EXPECT_EQ(conn.receive(), Connection::IoResult::peer_closed);
}

TEST(AgentConnectionTest, ResetReturnsToIdle) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
    Connection conn;
    make_connected(conn, sv[0]);
    Fd const peer{sv[1]};

    conn.reset();
    EXPECT_EQ(conn.state(), Connection::State::idle);
    EXPECT_EQ(conn.rx_buffer().size(), 0u);
    EXPECT_TRUE(conn.tx_empty());
}

TEST(AgentConnectionTest, TransmitReportsWouldBlockThenResumes) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
    int const small = 1024; // 커널 floor를 적용하므로 정확한 값은 아니나 버퍼를 작게 유도
    ::setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
    ::setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));
    Connection conn;
    make_connected(conn, sv[0]);
    Fd peer{sv[1]};

    // 소켓 버퍼 합보다 훨씬 큰 payload를 넣고 peer가 읽지 않으면 부분 송신 -> would_block.
    constexpr std::size_t big = 1u << 20; // 1 MiB
    auto pool = ddcs::common::ObjectPool<LinearBuffer>::create<1>(big);
    auto out = pool.acquire();
    std::vector<std::byte> const payload(big, std::byte{'z'});
    ASSERT_TRUE(out->try_append({payload.data(), payload.size()}));
    conn.tx_enqueue(std::move(out));

    EXPECT_EQ(conn.transmit(), Connection::IoResult::would_block);
    EXPECT_FALSE(conn.tx_empty()); // 부분 송신 후 큐에 보류

    // peer가 읽어 커널 버퍼를 비우면 재개되어 결국 전부 빠진다(ok + tx_empty).
    std::array<std::byte, 1 << 16> drain{};
    for (int i = 0; i < 100000 && !conn.tx_empty(); ++i) {
        while (::read(peer.get(), drain.data(), drain.size()) > 0) {
        }
        if (conn.transmit() == Connection::IoResult::ok) {
            break;
        }
    }
    EXPECT_TRUE(conn.tx_empty());
}

} // namespace
