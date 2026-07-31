#include "ddcs/net/stream_io.hpp"

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/fd.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <queue>
#include <vector>

#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::common::CircularBuffer;
using ddcs::common::LinearBuffer;
using ddcs::common::ObjectPool;
using ddcs::common::PoolHandle;
using ddcs::io::Fd;
using ddcs::net::receive_into;
using ddcs::net::ReceiveResult;
using ddcs::net::TransmitResult;
using ddcs::net::transmit_from;

// nonblocking AF_UNIX socketpair
struct SocketPair {
    Fd local; // 테스트 대상 fd
    Fd peer;

    SocketPair() {
        int sv[2] = {-1, -1};
        EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
        local = Fd{sv[0]};
        peer = Fd{sv[1]};
    }
};

TEST(ReceiveIntoTest, ReadsAvailableThenWouldBlock) {
    SocketPair sp;
    char const msg[] = "hello";
    ASSERT_EQ(::write(sp.peer.get(), msg, 5), 5);

    CircularBuffer rx{64};
    EXPECT_EQ(
        receive_into(sp.local.get(), rx).code, ReceiveResult::Code::would_block
    ); // 소진 후 EAGAIN
    EXPECT_EQ(rx.size(), 5u);

    std::array<std::byte, 5> got{};
    EXPECT_TRUE(rx.read({got.data(), got.size()}));
    EXPECT_EQ(std::memcmp(got.data(), msg, 5), 0);
}

TEST(ReceiveIntoTest, ReportsPeerClosed) {
    SocketPair sp;
    sp.peer.close(); // FIN

    CircularBuffer rx{64};
    EXPECT_EQ(receive_into(sp.local.get(), rx).code, ReceiveResult::Code::peer_closed);
}

TEST(ReceiveIntoTest, ReportsFullWhenRxBufferFull) {
    SocketPair sp;
    std::array<char, 16> data{};
    data.fill('x');
    ASSERT_EQ(::write(sp.peer.get(), data.data(), data.size()), 16);

    CircularBuffer rx{8}; // 2의 거듭제곱, 도착분보다 작음
    EXPECT_EQ(receive_into(sp.local.get(), rx).code, ReceiveResult::Code::full);
    EXPECT_EQ(rx.size(), 8u);
}

TEST(ReceiveIntoTest, ReportsEconnresetErrnoOnRst) {
    // loopback TCP 쌍: SO_LINGER(0) close가 FIN 대신 RST를 보낸다.
    Fd listener{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    ASSERT_TRUE(listener.valid());
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listener.get(), 1), 0);
    socklen_t len = sizeof(addr);
    ASSERT_EQ(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &len), 0);

    Fd local{::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)};
    ASSERT_TRUE(local.valid());
    ASSERT_EQ(::connect(local.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    Fd remote{::accept(listener.get(), nullptr, nullptr)};
    ASSERT_TRUE(remote.valid());

    linger const lg{.l_onoff = 1, .l_linger = 0};
    ASSERT_EQ(::setsockopt(remote.get(), SOL_SOCKET, SO_LINGER, &lg, sizeof(lg)), 0);
    remote.close(); // RST

    // RST 도착 대기 (loopback이라 사실상 즉시)
    pollfd pfd{.fd = local.get(), .events = POLLIN, .revents = 0};
    ASSERT_GT(::poll(&pfd, 1, 1000), 0);

    CircularBuffer rx{64};
    auto const r = receive_into(local.get(), rx);
    EXPECT_EQ(r.code, ReceiveResult::Code::error);
    EXPECT_EQ(r.err, ECONNRESET);
}

TEST(TransmitFromTest, DrainsQueueAndReportsDrained) {
    SocketPair sp;
    auto pool = ObjectPool<LinearBuffer>::create<4>(std::size_t{64});
    std::queue<PoolHandle<LinearBuffer>> tx;
    auto buf = pool.acquire();
    char const msg[] = "world";
    EXPECT_TRUE(buf->append({reinterpret_cast<std::byte const*>(msg), 5}));
    tx.push(std::move(buf));

    EXPECT_EQ(transmit_from(sp.local.get(), tx).code, TransmitResult::Code::drained);
    EXPECT_TRUE(tx.empty());

    std::array<char, 8> got{};
    EXPECT_EQ(::read(sp.peer.get(), got.data(), got.size()), 5);
    EXPECT_EQ(std::memcmp(got.data(), msg, 5), 0);
}

TEST(TransmitFromTest, ReportsWouldBlockWhenSendBufferFull) {
    SocketPair sp;
    int const small = 1024;
    ::setsockopt(sp.local.get(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
    ::setsockopt(sp.peer.get(), SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));

    // peer는 읽지 않는다. 소켓 버퍼 합보다 훨씬 큰 payload를 넣어 부분 송신 -> EAGAIN을 강제
    constexpr std::size_t big = 4u << 20; // 4 MiB (어떤 socketpair 버퍼보다 크다)
    auto pool = ObjectPool<LinearBuffer>::create<1>(big);
    std::queue<PoolHandle<LinearBuffer>> tx;
    auto buf = pool.acquire();
    std::vector<std::byte> const payload(big, std::byte{'z'});
    ASSERT_TRUE(buf->append({payload.data(), payload.size()}));
    tx.push(std::move(buf));

    EXPECT_EQ(transmit_from(sp.local.get(), tx).code, TransmitResult::Code::would_block);
    EXPECT_FALSE(tx.empty()); // 일부만 보내고 큐에 남는다.
}

TEST(TransmitFromTest, ReportsEpipeErrnoAfterPeerClosed) {
    SocketPair sp;
    sp.peer.close();

    auto pool = ObjectPool<LinearBuffer>::create<4>(std::size_t{64});
    std::queue<PoolHandle<LinearBuffer>> tx;
    auto buf = pool.acquire();
    char const msg[] = "x";
    ASSERT_TRUE(buf->append({reinterpret_cast<std::byte const*>(msg), 1}));
    tx.push(std::move(buf));

    auto const r = transmit_from(sp.local.get(), tx);
    EXPECT_EQ(r.code, TransmitResult::Code::error);
    EXPECT_EQ(r.err, EPIPE); // MSG_NOSIGNAL: SIGPIPE 대신 errno
}

} // namespace
