#include "ddcs/agent/infra/connection.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <gtest/gtest.h>

#include <array>

#include <cstddef>
#include <cstring>

#include <sys/socket.h>
#include <unistd.h>

namespace {

using ddcs::agent::infra::Connection;
using ddcs::common::Fd;
using ddcs::common::LinearBuffer;

// socketpair 한쪽을 connected Connection 으로 만든다.
void make_connected(Connection& conn, int fd) {
    conn.assign(Fd{fd}, 0);
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
    EXPECT_EQ(conn.rx_size(), 5u);
    std::array<std::byte, 5> buf{};
    EXPECT_TRUE(conn.rx_read({buf.data(), buf.size()}));
    EXPECT_EQ(std::memcmp(buf.data(), msg, 5), 0);
}

TEST(AgentConnectionTest, TransmitDrainsTxQueue) {
    int sv[2];
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
    Connection conn;
    make_connected(conn, sv[0]);
    Fd peer{sv[1]};

    auto pool = ddcs::common::make_object_pool<LinearBuffer>(0, 4, std::size_t{64});
    auto out = pool.acquire();
    char const msg[] = "world";
    ASSERT_TRUE(out->write({reinterpret_cast<std::byte const*>(msg), 5}));
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
    } // 스코프 종료 -> close -> FIN

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
    EXPECT_EQ(conn.rx_size(), 0u);
    EXPECT_TRUE(conn.tx_empty());
}

} // namespace
