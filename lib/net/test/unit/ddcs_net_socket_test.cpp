#include "ddcs/net/socket.hpp"

#include "ddcs/io/fd.hpp"


#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <gtest/gtest.h>

namespace {

using ddcs::io::Fd;
using ddcs::net::bound_port;

TEST(BoundPortTest, ReturnsNulloptOnInvalidFd) {
    EXPECT_FALSE(bound_port(-1).has_value());
}

TEST(BoundPortTest, ReturnsEphemeralPort) {
    int const s = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(s, 0);
    Fd fd{s};

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0; // ephemeral
    addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
    ASSERT_EQ(::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

    // 커널이 실제 포트를 할당했다.
    auto const port = bound_port(fd.get());
    ASSERT_TRUE(port.has_value());
    EXPECT_NE(*port, 0u);

    // getsockname 직접 호출과 동일해야 한다.
    sockaddr_in got{};
    socklen_t len{sizeof(got)};
    ASSERT_EQ(::getsockname(fd.get(), reinterpret_cast<sockaddr*>(&got), &len), 0);
    EXPECT_EQ(*port, ::ntohs(got.sin_port));
}

} // namespace
