#include "ddcs/ctrl/controller.hpp"

#include "ddcs/logger/log.hpp"
#include "ddcs/profile/recorder.hpp"
#include "ddcs/profile/tick_sample.hpp"

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

// 로그를 수집해 정책 재로딩 여부와 결과를 확인한다.
class CapturingSink final : public ddcs::logger::Sink {
public:
    std::string text;
    void write(std::string_view line) noexcept override {
        text.append(line);
    }
};

// 테스트 로그 출력을 등록하고, 소멸 시 해제해 유효하지 않은 참조가 남지 않도록 한다.
// 실행 앱에서는 main이 로거를 설정한다.
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

// 겹치지 않는 부분 문자열의 개수를 세어 정책 로딩 성공 횟수를 확인한다.
// 닫는 따옴표까지 비교해 policy.load.fail 및 policy.load.absent와 구분한다.
std::size_t count_substr(std::string_view hay, std::string_view needle) {
    std::size_t n = 0;
    for (auto pos = hay.find(needle); pos != std::string_view::npos;
         pos = hay.find(needle, pos + needle.size())) {
        ++n;
    }
    return n;
}

// 로컬 메트릭 서버에 GET 요청을 보내고 Controller를 실행하며 응답 전체를 읽는다.
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
    bool complete = false;
    for (int i = 0; i < 40; ++i) {
        controller.run_once(std::chrono::milliseconds{50});
        char buf[4096];
        ssize_t const n = ::recv(cfd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n > 0) {
            resp.append(buf, static_cast<std::size_t>(n));
        }
        if (n == 0) { // Connection: close 응답은 EOF까지 읽는다.
            complete = true;
            break;
        }
    }
    ::close(cfd);
    EXPECT_TRUE(complete);
    return resp;
}

// 조립 루트 스모크: 구성 후 start, ephemeral 바인드, 이벤트 루프를 한 번 실행한다., stop까지 무사한지
TEST(ControllerTest, StartsBindsEphemeralPortAndDispatchesOnce) {
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.accept_backlog = 16;

    Controller controller{cfg};
    controller.start();
    EXPECT_NE(controller.port(), 0); // 운영체제가 실제 포트를 배정한다.

    controller.run_once(std::chrono::milliseconds{10}); // 클라이언트 없이 이벤트 루프를 실행한다.
    controller.stop();
}

TEST(ControllerTest, RecordsCompletedTicksWhenARecorderIsProvided) {
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};
    cfg.sweep_interval = std::chrono::milliseconds{1};
    ddcs::profile::Recorder recorder{4};

    Controller controller{cfg, &recorder};
    controller.start();
    for (int i = 0; i < 10; ++i) {
        controller.run_once(std::chrono::milliseconds{10});
    }
    controller.stop();

    auto const recording = recorder.finish();
    ASSERT_FALSE(recording.samples().empty());
    for (auto const& sample : recording.samples()) {
        EXPECT_EQ(sample.outcome, ddcs::profile::TickOutcome::completed);
        EXPECT_EQ(sample.policy_evaluate_ended_ns, sample.finished_ns);
        EXPECT_TRUE(ddcs::profile::is_valid_tick_sample(sample));
    }
}

// 이미 사용 중인 포트로 시작하면 예외에 포트 번호와 EADDRINUSE가 포함된다.
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

// 메트릭 포트가 설정되지 않으면 서버를 열지 않고 포트 번호로 0을 반환한다.
TEST(ControllerTest, DisablesMetricsByDefault) {
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};

    Controller controller{cfg};
    controller.start();
    EXPECT_EQ(controller.prometheus_port(), 0);
    controller.stop();
}

// 메트릭 포트를 설정하면 GET /metrics로 레지스트리의 현재 상태를 조회할 수 있다.
TEST(ControllerTest, ServesMetricsWhenEnabled) {
    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::warn};
    Controller::Config cfg{};
    cfg.prometheus_port = 0; // 메트릭 서버를 열고 운영체제에 포트 배정을 맡긴다.

    Controller controller{cfg};
    controller.start();
    ASSERT_NE(controller.prometheus_port(), 0);

    auto const resp = scrape_metrics(controller, controller.prometheus_port());
    EXPECT_NE(resp.find("200 OK"), std::string::npos);
    EXPECT_NE(resp.find("# TYPE ddcs_connections gauge"), std::string::npos);
    EXPECT_NE(resp.find("ddcs_connections 0"), std::string::npos); // 연결된 세션 없음
    EXPECT_NE(resp.find("# TYPE ddcs_tick_start_lateness_seconds gauge"), std::string::npos);
    EXPECT_NE(resp.find("# TYPE ddcs_tick_start_lateness_seconds_max gauge"), std::string::npos);
    EXPECT_NE(resp.find("ddcs_tick_skipped_total 0\n"), std::string::npos);
    controller.stop();
}

// SIGHUP 수신 시 파일에서 정책을 다시 읽어 적용한다. 다른 설정은 유지한다.
TEST(ControllerTest, SighupReloadsPolicy) {
    auto const path = std::filesystem::temp_directory_path() / "ddcs_reload_test.json";
    write_file(
        path, R"({"policy":{"groups":{"alpha":{"busy_load":80,"idle_load":20,)"
              R"("busy_mode":"performance","idle_mode":"normal"}}}})"
    );

    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::info}; // 정책 로딩 로그를 수집한다.
    Controller::Config cfg{};
    cfg.policy_path = path;

    Controller controller{cfg};
    controller.start(); // Group 1개인 정책으로 시작한다.

    // Group 2개인 정책으로 파일을 바꾸고 SIGHUP을 보낸다.
    write_file(
        path,
        R"({"policy":{"groups":{)"
        R"("alpha":{"busy_load":80,"idle_load":20,"busy_mode":"performance","idle_mode":"normal"},)"
        R"("beta":{"busy_load":60,"idle_load":40,"busy_mode":"performance","idle_mode":"normal"}}}})"
    );
    ::raise(SIGHUP);
    for (int i = 0; i < 10 && sink.text.find(R"("trigger":"reload")") == std::string::npos; ++i) {
        controller.run_once(std::chrono::milliseconds{20}); // 시그널을 처리해 정책을 다시 읽는다.
    }
    controller.stop();

    EXPECT_NE(sink.text.find(R"("trigger":"reload")"), std::string::npos); // SIGHUP으로 정책을 다시 읽었다.
    EXPECT_NE(sink.text.find(R"("groups":2)"), std::string::npos); // Group 2개인 새 정책을 적용했다.

    std::filesystem::remove(path);
}

// 정책을 다시 읽을 때 JSON 문법이 잘못되어 있으면 기존 정책을 유지한다.
TEST(ControllerTest, SighupWithMalformedPolicyKeepsOldPolicy) {
    auto const path = std::filesystem::temp_directory_path() / "ddcs_reload_malformed_test.json";
    write_file(
        path, R"({"policy":{"groups":{"alpha":{"busy_load":80,"idle_load":20,)"
              R"("busy_mode":"performance","idle_mode":"normal"}}}})"
    );

    CapturingSink sink;
    ScopedLogger logger{sink, ddcs::logger::Level::info}; // 정책 로딩 결과를 수집한다.
    Controller::Config cfg{};
    cfg.policy_path = path;

    Controller controller{cfg};
    controller.start(); // 유효한 정책을 한 번 적용한다.

    // 잘못된 JSON으로 파일을 바꾸고 SIGHUP을 보내 파싱 실패를 확인한다.
    write_file(path, "{ this is not json");
    ::raise(SIGHUP);
    for (int i = 0; i < 10 && sink.text.find(R"("reason":"parse")") == std::string::npos; ++i) {
        controller.run_once(std::chrono::milliseconds{20});
    }
    controller.stop();

    EXPECT_NE(sink.text.find(R"("trigger":"reload")"), std::string::npos); // 정책 재로딩 요청 처리
    EXPECT_NE(sink.text.find(R"("event":"policy.load.fail")"), std::string::npos); // 잘못된 JSON 거부
    // 정책 적용 성공 횟수는 시작 시 한 번이며, 재로딩 실패 후에는 증가하지 않는다.
    EXPECT_EQ(count_substr(sink.text, R"("event":"policy.load")"), 1U);

    std::filesystem::remove(path);
}

// JSON 문법이 맞아도 정책의 필수 필드가 빠져 있으면 기존 정책을 유지한다.
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

    // idle_load와 mode가 없는 정책은 검증에 실패하므로 적용하지 않는다.
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
