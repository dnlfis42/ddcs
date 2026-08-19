#include "ddcs/ctrl/controller.hpp"

#include "ddcs/logger/log.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::ctrl::Controller;

// 로그 라인을 모으는 sink. 핫리로드 발생/적용을 device 없이 관측한다.
class CapturingSink final : public ddcs::logger::Sink {
public:
    std::string text;
    void write(std::string_view line) noexcept override {
        text.append(line);
    }
};

// 전역 로거를 테스트 sink로 세우고, 파괴 시 떼어내 stack sink dangling을 막는다.
// (production은 main이 같은 부트스트랩을 수행한다)
class ScopedLogger {
public:
    ScopedLogger(ddcs::logger::Sink& sink, ddcs::logger::Level level)
        : sink_(sink) {
        auto& lg = ddcs::logger::Logger::instance();
        lg.set_level(level);
        lg.set_sink(sink);
    }
    ~ScopedLogger() {
        ddcs::logger::Logger::instance().clear_sink(sink_);
    }

    ScopedLogger(ScopedLogger const&) = delete;
    ScopedLogger& operator=(ScopedLogger const&) = delete;

private:
    ddcs::logger::Sink& sink_;
};

void write_file(std::filesystem::path const& p, std::string_view content) {
    std::ofstream out{p};
    out << content;
}

// 부분 문자열 발생 횟수(겹침 없음). 핫리로드 성공 토큰 카운트에 쓴다.
// 성공 이벤트는 "event":"policy.load"(닫는 따옴표 포함)라 .fail/.absent 와 안 겹친다.
std::size_t count_substr(std::string_view hay, std::string_view needle) {
    std::size_t n = 0;
    for (auto pos = hay.find(needle); pos != std::string_view::npos;
         pos = hay.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

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
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.accept_backlog = 16;

    Controller controller{cfg};
    controller.start();
    EXPECT_NE(controller.port(), 0); // 0이 실제 바인드 포트로 치환됨

    controller.run_once(std::chrono::milliseconds{10}); // 클라이언트 없음, 루프 무사 통과
    controller.stop();
}

// 부팅 실패 진단: 점유된 포트로 start하면 포트와 EADDRINUSE가 예외에 실린다
TEST(ControllerTest, ReportsAddressInUseOnOccupiedPort) {
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.accept_backlog = 16;

    Controller occupant{cfg};
    occupant.start();
    ASSERT_NE(occupant.port(), 0);

    Controller::Config conflicting{};
    conflicting.listen_port = occupant.port();
    conflicting.accept_backlog = 16;

    Controller latecomer{conflicting};
    try {
        latecomer.start();
        FAIL() << "occupied port must fail start()";
    } catch (std::system_error const& e) {
        EXPECT_EQ(e.code().value(), EADDRINUSE);
        EXPECT_NE(std::string{e.what()}.find("transport listen port"), std::string::npos);
    }
    occupant.stop();
}

// prometheus_port nullopt(기본)면 엔드포인트 비활성: 바인드 포트 0
TEST(ControllerTest, DisablesMetricsByDefault) {
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};

    Controller controller{cfg};
    controller.start();
    EXPECT_EQ(controller.prometheus_port(), 0);
    controller.stop();
}

// prometheus_port 지정 시 엔드포인트 활성: GET /metrics가 실제 레지스트리 gauge를 노출
TEST(ControllerTest, ServesMetricsWhenEnabled) {
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};
    cfg.prometheus_port = 0; // ephemeral, 활성

    Controller controller{cfg};
    controller.start();
    ASSERT_NE(controller.prometheus_port(), 0);

    auto const resp = scrape_metrics(controller, controller.prometheus_port());
    EXPECT_NE(resp.find("200 OK"), std::string::npos);
    EXPECT_NE(resp.find("# TYPE ddcs_connections gauge"), std::string::npos);
    EXPECT_NE(resp.find("ddcs_connections 0"), std::string::npos); // session 없음
    controller.stop();
}

// SIGHUP -> 정책 핫리로드: 새 파일을 다시 읽어 set_policy 한다(다른 설정은 부팅 시 고정).
TEST(ControllerTest, SighupReloadsPolicy) {
    auto const path = std::filesystem::temp_directory_path() / "ddcs_reload_test.json";
    write_file(
        path, R"({"policy":{"groups":{"alpha":{"busy_load":80,"idle_load":20,)"
              R"("busy_mode":"performance","idle_mode":"normal"}}}})"
    );

    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::info}; // policy.load* 가 보이게
    Controller::Config cfg{};
    cfg.policy_path = path;

    Controller controller{cfg};
    controller.start(); // policy A(group 1개) 로드 -> policy.load groups=1 trigger=boot

    // 파일을 group 2개로 교체 후 SIGHUP -> 핫리로드(재적용)
    write_file(
        path,
        R"({"policy":{"groups":{)"
        R"("alpha":{"busy_load":80,"idle_load":20,"busy_mode":"performance","idle_mode":"normal"},)"
        R"("beta":{"busy_load":60,"idle_load":40,"busy_mode":"performance","idle_mode":"normal"}}}})"
    );
    ::raise(SIGHUP);
    for (int i = 0; i < 10 && sink.text.find(R"("trigger":"reload")") == std::string::npos; ++i) {
        controller.run_once(std::chrono::milliseconds{20}); // signalfd 처리 -> reload
    }
    controller.stop();

    EXPECT_NE(sink.text.find(R"("trigger":"reload")"), std::string::npos); // SIGHUP 트리거됨
    EXPECT_NE(sink.text.find(R"("groups":2)"), std::string::npos); // 새 파일(group 2개) 적용됨

    std::filesystem::remove(path);
}

// 핫리로드 안전속성: SIGHUP 때 파일이 malformed(깨진 JSON)면 옛 정책을 그대로 유지한다.
// 운영자 오타가 동작 중인 fleet 정책을 지워버리면 안 된다(validate-before-apply).
// SighupReloadsPolicy는 성공 경로만 보므로 이 keep-old 분기는 여기서만 커버된다(parse_fail).
TEST(ControllerTest, SighupWithMalformedPolicyKeepsOldPolicy) {
    auto const path = std::filesystem::temp_directory_path() / "ddcs_reload_malformed_test.json";
    write_file(
        path, R"({"policy":{"groups":{"alpha":{"busy_load":80,"idle_load":20,)"
              R"("busy_mode":"performance","idle_mode":"normal"}}}})"
    );

    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::info}; // policy.load / policy.load.* 가 보이게
    Controller::Config cfg{};
    cfg.policy_path = path;

    Controller controller{cfg};
    controller.start(); // 유효한 정책 A 로드 -> "policy.load" 1회

    // 깨진 JSON으로 교체 후 SIGHUP -> json parse 실패 -> set_policy 미호출(옛 정책 유지)
    write_file(path, "{ this is not json");
    ::raise(SIGHUP);
    for (int i = 0; i < 10 && sink.text.find(R"("reason":"parse")") == std::string::npos; ++i) {
        controller.run_once(std::chrono::milliseconds{20});
    }
    controller.stop();

    EXPECT_NE(sink.text.find(R"("trigger":"reload")"), std::string::npos);         // SIGHUP 처리됨
    EXPECT_NE(sink.text.find(R"("event":"policy.load.fail")"), std::string::npos); // malformed 거부
    // 성공 토큰은 부팅 1회 그대로 -- 재적용이 없었다 = 옛 정책 유지.
    EXPECT_EQ(count_substr(sink.text, R"("event":"policy.load")"), 1U);

    std::filesystem::remove(path);
}

// 핫리로드 안전속성(의미 오류판): 문법은 맞지만 필수 필드 누락으로 정책이 invalid면 옛 정책 유지.
TEST(ControllerTest, SighupWithInvalidPolicyKeepsOldPolicy) {
    auto const path = std::filesystem::temp_directory_path() / "ddcs_reload_invalid_test.json";
    write_file(
        path, R"({"policy":{"groups":{"alpha":{"busy_load":80,"idle_load":20,)"
              R"("busy_mode":"performance","idle_mode":"normal"}}}})"
    );

    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::info};
    Controller::Config cfg{};
    cfg.policy_path = path;

    Controller controller{cfg};
    controller.start();

    // 파싱은 되지만 idle_load/mode 누락 -> parse_policy nullopt -> policy.load.fail reason=invalid
    // (set_policy 미호출)
    write_file(path, R"({"policy":{"groups":{"alpha":{"busy_load":80}}}})");
    ::raise(SIGHUP);
    for (int i = 0; i < 10 && sink.text.find(R"("reason":"invalid")") == std::string::npos; ++i) {
        controller.run_once(std::chrono::milliseconds{20});
    }
    controller.stop();

    EXPECT_NE(sink.text.find(R"("reason":"invalid")"), std::string::npos);
    EXPECT_EQ(count_substr(sink.text, R"("event":"policy.load")"), 1U); // 옛 정책 유지

    std::filesystem::remove(path);
}

} // namespace
