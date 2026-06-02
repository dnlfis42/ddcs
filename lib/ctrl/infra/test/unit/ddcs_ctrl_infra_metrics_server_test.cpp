#include "ddcs/ctrl/infra/metrics/server.hpp"

#include "ddcs/ctrl/port/metrics/inbound.hpp"
#include "ddcs/io/reactor.hpp"

#include <gtest/gtest.h>

#include <string>

#include <cstddef>
#include <cstdint>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using ddcs::ctrl::infra::metrics::Server;
using ddcs::io::Reactor;
using namespace std::chrono_literals;

class FakeProvider final : public ddcs::ctrl::port::metrics::Inbound {
public:
    std::string scrape() override { return "test_metric 42\n"; }
};

// 127.0.0.1:port 로 connect -> request 전송 -> reactor 구동하며 전체 응답 read.
std::string scrape_over_socket(std::uint16_t port, Reactor& reactor, std::string const& request) {
    int const cfd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(cfd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    EXPECT_EQ(::connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    EXPECT_EQ(::send(cfd, request.data(), request.size(), 0), static_cast<ssize_t>(request.size()));

    std::string resp;
    for (int i = 0; i < 40; ++i) {
        reactor.run_once(50ms); // 서버: accept -> read -> respond -> close
        char buf[4096];
        ssize_t const n = ::recv(cfd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
            resp.append(buf, static_cast<std::size_t>(n));
        }
        if (resp.find("test_metric") != std::string::npos) {
            break;
        }
    }
    ::close(cfd);
    return resp;
}

} // namespace

TEST(MetricsServerTest, ServesScrapeOverHttp) {
    Reactor reactor;
    FakeProvider provider;
    Server server{reactor, provider, 0, 16}; // ephemeral 포트
    server.start();
    ASSERT_NE(server.port(), 0);

    auto const resp = scrape_over_socket(server.port(), reactor, "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_NE(resp.find("200 OK"), std::string::npos);
    EXPECT_NE(resp.find("text/plain"), std::string::npos);
    EXPECT_NE(resp.find("test_metric 42"), std::string::npos);
    EXPECT_EQ(server.size(), 0u); // 응답 완료 후 reap 됨
}
