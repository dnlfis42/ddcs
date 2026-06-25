#include "ddcs/ctrl/controller.hpp"

#include "ddcs/logger/log.hpp"

#include <chrono>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::Controller;

// 127.0.0.1:port로 GET 후, controller를 구동하며 전체 응답을 read.
std::string scrape_metrics(Controller& controller, std::uint16_t port) {
    int const cfd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(cfd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    EXPECT_EQ(::connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    std::string const req{"GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n"};
    EXPECT_EQ(::send(cfd, req.data(), req.size(), 0), static_cast<ssize_t>(req.size()));

    std::string resp;
    for (int i = 0; i < 40; ++i) {
        controller.run_once(std::chrono::milliseconds{50});
        char buf[4096];
        ssize_t const n = ::recv(cfd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
            resp.append(buf, static_cast<std::size_t>(n));
        }
        if (resp.find("ddcs_connections") != std::string::npos) {
            break;
        }
    }
    ::close(cfd);
    return resp;
}

// 조립 루트 스모크: 구성 후 start, ephemeral 바인드, 1회 디스패치, stop까지 무사한지
TEST(ControllerTest, StartsBindsEphemeralPortAndDispatchesOnce) {
    Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.accept_backlog = 16;
    cfg.log_level = ddcs::logger::Level::warn;

    Controller controller{cfg};
    controller.start();
    EXPECT_NE(controller.port(), 0); // 0이 실제 바인드 포트로 치환됨

    controller.run_once(std::chrono::milliseconds{10}); // 클라이언트 없음, 루프 무사 통과
    controller.stop();
}

// metrics_port nullopt(기본)면 엔드포인트 비활성: 바인드 포트 0
TEST(ControllerTest, DisablesMetricsByDefault) {
    Controller::Config cfg{};
    cfg.log_level = ddcs::logger::Level::warn;

    Controller controller{cfg};
    controller.start();
    EXPECT_EQ(controller.metrics_port(), 0);
    controller.stop();
}

// metrics_port 지정 시 엔드포인트 활성: GET /metrics가 실제 레지스트리 gauge를 노출
TEST(ControllerTest, ServesMetricsWhenEnabled) {
    Controller::Config cfg{};
    cfg.metrics_port = 0; // ephemeral, 활성
    cfg.log_level = ddcs::logger::Level::warn;

    Controller controller{cfg};
    controller.start();
    ASSERT_NE(controller.metrics_port(), 0);

    auto const resp = scrape_metrics(controller, controller.metrics_port());
    EXPECT_NE(resp.find("200 OK"), std::string::npos);
    EXPECT_NE(resp.find("# TYPE ddcs_connections gauge"), std::string::npos);
    EXPECT_NE(resp.find("ddcs_connections 0"), std::string::npos); // session 없음
    controller.stop();
}

} // namespace
