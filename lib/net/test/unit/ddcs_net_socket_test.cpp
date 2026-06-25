#include "ddcs/net/socket.hpp"

#include "ddcs/io/fd.hpp"

#include <cstdint>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

namespace {

using ddcs::io::Fd;
using ddcs::net::bound_port;

TEST(NetSocketTest, BoundPortReturnsEphemeralPort) {
    int const s = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(s, 0);
    Fd fd{s};

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral
    ASSERT_EQ(::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

    std::uint16_t const port = bound_port(fd.get());
    EXPECT_NE(port, 0u); // 커널이 실제 포트를 할당했다

    // getsockname 직접 호출과 동일해야 한다
    sockaddr_in got{};
    socklen_t len{sizeof(got)};
    ASSERT_EQ(::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&got), &len), 0);
    EXPECT_EQ(port, ::ntohs(got.sin_port));
}

TEST(NetSocketTest, BoundPortReturnsZeroOnInvalidFd) {
    EXPECT_EQ(bound_port(-1), 0u); // getsockname 실패 -> 0
}

} // namespace
