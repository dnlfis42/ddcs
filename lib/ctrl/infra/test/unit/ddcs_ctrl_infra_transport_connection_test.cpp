#include "ddcs/ctrl/infra/transport/connection.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <utility>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using ddcs::common::Fd;
using ddcs::ctrl::infra::transport::Connection;
using ddcs::ctrl::infra::transport::Endpoint;
using ddcs::ctrl::infra::transport::inbound_buffer_capacity;
using ddcs::ctrl::port::transport::ConnectionId;

using S = Connection::State;
using R = Connection::IoResult;

// 합법 엣지로만 목표 상태까지 구동.
void drive_to(Connection& c, S target) {
    switch (target) {
    case S::idle:
        break;
    case S::open:
        ASSERT_TRUE(c.transition(S::open));
        break;
    case S::active_close:
        ASSERT_TRUE(c.transition(S::open));
        ASSERT_TRUE(c.transition(S::active_close));
        break;
    case S::passive_wait:
        ASSERT_TRUE(c.transition(S::open));
        ASSERT_TRUE(c.transition(S::active_close));
        ASSERT_TRUE(c.transition(S::passive_wait));
        break;
    case S::passive_close:
        ASSERT_TRUE(c.transition(S::open));
        ASSERT_TRUE(c.transition(S::passive_close));
        break;
    case S::aborting:
        ASSERT_TRUE(c.transition(S::open));
        ASSERT_TRUE(c.transition(S::aborting));
        break;
    case S::closing:
        ASSERT_TRUE(c.transition(S::open));
        ASSERT_TRUE(c.transition(S::closing));
        break;
    }
}

} // namespace

TEST(ConnectionTransitionTest, LegalEdgesSucceedAndAdvanceState) {
    struct Edge {
        S from;
        S to;
    };
    constexpr Edge legal[] = {
        {S::idle, S::open},
        {S::idle, S::closing},
        {S::open, S::active_close},
        {S::open, S::passive_close},
        {S::open, S::aborting},
        {S::open, S::closing},
        {S::active_close, S::passive_wait},
        {S::active_close, S::closing},
        {S::passive_wait, S::closing},
        {S::passive_close, S::aborting},
        {S::passive_close, S::closing},
        {S::aborting, S::closing},
        {S::closing, S::idle},
    };
    for (auto const e : legal) {
        Connection c;
        drive_to(c, e.from);
        EXPECT_TRUE(c.transition(e.to));
        EXPECT_EQ(c.state(), e.to);
    }
}

TEST(ConnectionTransitionTest, IllegalEdgesRejectedAndStateUnchanged) {
    struct Edge {
        S from;
        S to;
    };
    constexpr Edge illegal[] = {
        {S::idle, S::idle},          {S::idle, S::aborting},   {S::idle, S::passive_close},
        {S::open, S::idle},          {S::open, S::open},       {S::open, S::passive_wait},
        {S::passive_close, S::open}, {S::aborting, S::idle},   {S::aborting, S::passive_close},
        {S::closing, S::open},       {S::closing, S::closing},
    };
    for (auto const e : illegal) {
        Connection c;
        drive_to(c, e.from);
        EXPECT_FALSE(c.transition(e.to));
        EXPECT_EQ(c.state(), e.from);
    }
}

TEST(ConnectionStateTest, EpollMirrorTogglesAndInterestStored) {
    Connection c;
    EXPECT_FALSE(c.in_epoll());
    c.enter_epoll();
    EXPECT_TRUE(c.in_epoll());
    c.leave_epoll();
    EXPECT_FALSE(c.in_epoll());

    c.set_io_interest(0x55);
    EXPECT_EQ(c.io_interest(), 0x55u);
}

class ConnectionIoTest : public testing::Test {
protected:
    void SetUp() override {
        int fds[2];
        ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
        int const fl = ::fcntl(fds[0], F_GETFL, 0);
        ASSERT_EQ(::fcntl(fds[0], F_SETFL, fl | O_NONBLOCK), 0); // conn 측 nonblocking
        peer_fd_ = fds[1];
        conn_.assign(ConnectionId{std::uint64_t{1}}, Fd{fds[0]}, Endpoint{}, 0x2A);
    }
    void TearDown() override {
        if (peer_fd_ >= 0) {
            ::close(peer_fd_);
        }
    }

    int peer_fd_{-1};
    Connection conn_;
};

TEST_F(ConnectionIoTest, AssignStoresFieldsWithoutTransition) {
    EXPECT_EQ(conn_.id().value(), 1u);
    EXPECT_GE(conn_.fd(), 0);
    EXPECT_EQ(conn_.io_interest(), 0x2Au);
    EXPECT_EQ(conn_.state(), S::idle);
    EXPECT_FALSE(conn_.in_epoll());
}

TEST_F(ConnectionIoTest, ReceiveReturnsWouldBlockWhenEmpty) {
    EXPECT_EQ(conn_.receive(), R::would_block);
    EXPECT_EQ(conn_.rx_size(), 0u);
}

TEST_F(ConnectionIoTest, ReceiveDrainsPeerDataThenWouldBlock) {
    char const msg[] = "abcd";
    ASSERT_EQ(::write(peer_fd_, msg, 4), 4);

    EXPECT_EQ(conn_.receive(), R::would_block);
    ASSERT_EQ(conn_.rx_size(), 4u);

    std::array<std::byte, 4> got{};
    EXPECT_TRUE(conn_.rx_read(got));
    EXPECT_EQ(std::memcmp(got.data(), msg, 4), 0);
    EXPECT_EQ(conn_.rx_size(), 0u);
}

TEST_F(ConnectionIoTest, ReceiveReturnsPeerClosedOnFin) {
    ::close(peer_fd_); // FIN
    peer_fd_ = -1;
    EXPECT_EQ(conn_.receive(), R::peer_closed);
}

TEST_F(ConnectionIoTest, ReceiveReturnsFullWhenBufferSaturated) {
    std::array<char, inbound_buffer_capacity + 1000> big{};
    ASSERT_EQ(::write(peer_fd_, big.data(), big.size()), static_cast<ssize_t>(big.size()));

    EXPECT_EQ(conn_.receive(), R::full);
    EXPECT_EQ(conn_.rx_size(), inbound_buffer_capacity);
}

TEST_F(ConnectionIoTest, TransmitSendsQueuedDataThenEmpties) {
    auto pool = ddcs::common::make_pool<ddcs::common::LinearBuffer>(1, 1, std::size_t{256});
    auto buf = pool.acquire();
    char const msg[] = "hello";
    ASSERT_TRUE(buf->write({reinterpret_cast<std::byte const*>(msg), 5}));
    conn_.tx_enqueue(std::move(buf));

    EXPECT_EQ(conn_.transmit(), R::ok);
    EXPECT_TRUE(conn_.tx_empty());

    char out[16] = {};
    EXPECT_EQ(::read(peer_fd_, out, sizeof(out)), 5);
    EXPECT_EQ(std::memcmp(out, msg, 5), 0);
}

TEST_F(ConnectionIoTest, LatchRstSetsLingerZero) {
    conn_.latch_rst();

    linger lg{};
    socklen_t len{sizeof(lg)};
    ASSERT_EQ(::getsockopt(conn_.fd(), SOL_SOCKET, SO_LINGER, &lg, &len), 0);
    EXPECT_NE(lg.l_onoff, 0);
    EXPECT_EQ(lg.l_linger, 0);
}

TEST_F(ConnectionIoTest, ResetRestoresIdleAndClosesFd) {
    ASSERT_TRUE(conn_.transition(S::open));
    conn_.enter_epoll();
    char const msg[] = "x";
    ASSERT_EQ(::write(peer_fd_, msg, 1), 1);
    ASSERT_EQ(conn_.receive(), R::would_block);
    ASSERT_EQ(conn_.rx_size(), 1u);

    conn_.reset();

    EXPECT_EQ(conn_.state(), S::idle);
    EXPECT_EQ(conn_.fd(), Fd::invalid);
    EXPECT_FALSE(conn_.in_epoll());
    EXPECT_EQ(conn_.rx_size(), 0u);
    EXPECT_TRUE(conn_.tx_empty());
}
