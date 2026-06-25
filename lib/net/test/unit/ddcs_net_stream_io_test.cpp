#include "ddcs/net/stream_io.hpp"

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/fd.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <queue>
#include <vector>

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
using ddcs::net::StreamResult;
using ddcs::net::transmit_from;

// nonblocking AF_UNIX socketpair. [0]=local(테스트 대상 fd), [1]=peer
struct SocketPair {
    Fd local;
    Fd peer;

    SocketPair() {
        int sv[2] = {-1, -1};
        EXPECT_EQ(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
        local = Fd{sv[0]};
        peer = Fd{sv[1]};
    }
};

TEST(StreamIoTest, ReceiveIntoReadsAvailableThenWouldBlock) {
    SocketPair sp;
    char const msg[] = "hello";
    ASSERT_EQ(::write(sp.peer.get(), msg, 5), 5);

    CircularBuffer rx{64};
    EXPECT_EQ(receive_into(sp.local.get(), rx), StreamResult::would_block); // 소진 후 EAGAIN
    EXPECT_EQ(rx.size(), 5u);

    std::array<std::byte, 5> got{};
    EXPECT_TRUE(rx.try_read({got.data(), got.size()}));
    EXPECT_EQ(std::memcmp(got.data(), msg, 5), 0);
}

TEST(StreamIoTest, ReceiveIntoReportsPeerClosed) {
    SocketPair sp;
    sp.peer.close(); // FIN

    CircularBuffer rx{64};
    EXPECT_EQ(receive_into(sp.local.get(), rx), StreamResult::peer_closed);
}

TEST(StreamIoTest, ReceiveIntoReportsFullWhenRxBufferFull) {
    SocketPair sp;
    std::array<char, 16> data{};
    data.fill('x');
    ASSERT_EQ(::write(sp.peer.get(), data.data(), data.size()), 16);

    CircularBuffer rx{8}; // 2의 거듭제곱, 도착분보다 작음
    EXPECT_EQ(receive_into(sp.local.get(), rx), StreamResult::full);
    EXPECT_EQ(rx.size(), 8u);
}

TEST(StreamIoTest, TransmitFromDrainsQueueAndReportsOk) {
    SocketPair sp;
    auto pool = ObjectPool<LinearBuffer>::create<4>(std::size_t{64});
    std::queue<PoolHandle<LinearBuffer>> tx;
    auto buf = pool.acquire();
    char const msg[] = "world";
    EXPECT_TRUE(buf->try_append({reinterpret_cast<std::byte const*>(msg), 5}));
    tx.push(std::move(buf));

    EXPECT_EQ(transmit_from(sp.local.get(), tx), StreamResult::ok);
    EXPECT_TRUE(tx.empty());

    std::array<char, 8> got{};
    EXPECT_EQ(::read(sp.peer.get(), got.data(), got.size()), 5);
    EXPECT_EQ(std::memcmp(got.data(), msg, 5), 0);
}

TEST(StreamIoTest, TransmitFromReportsWouldBlockWhenSendBufferFull) {
    SocketPair sp;
    int const small = 1024;
    ::setsockopt(sp.local.get(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));
    ::setsockopt(sp.peer.get(), SOL_SOCKET, SO_RCVBUF, &small, sizeof(small));

    // peer는 읽지 않는다. 소켓 버퍼 합보다 훨씬 큰 payload를 넣어 부분 송신 -> EAGAIN을 강제.
    constexpr std::size_t big = 4u << 20; // 4 MiB (어떤 socketpair 버퍼보다 크다)
    auto pool = ObjectPool<LinearBuffer>::create<1>(big);
    std::queue<PoolHandle<LinearBuffer>> tx;
    auto buf = pool.acquire();
    std::vector<std::byte> const payload(big, std::byte{'z'});
    ASSERT_TRUE(buf->try_append({payload.data(), payload.size()}));
    tx.push(std::move(buf));

    EXPECT_EQ(transmit_from(sp.local.get(), tx), StreamResult::would_block);
    EXPECT_FALSE(tx.empty()); // 일부만 보내고 큐에 남는다
}

} // namespace
