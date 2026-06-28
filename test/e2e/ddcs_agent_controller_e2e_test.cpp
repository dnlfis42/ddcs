#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/app/session/session_service.hpp"
#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/controller.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using State = ddcs::agent::app::session::SessionService::State;

// 양쪽(Controller/Agent) 로그를 한 sink에 모은다.
// logger singleton이라 마지막 set_sink가 이김
// -> 두 facade Config에 같은 인스턴스를 명시 주입한다.
class CaptureSink : public ddcs::logger::Sink {
public:
    bool contains(std::string_view needle) {
        std::lock_guard<std::mutex> lk{m_};
        return std::any_of(lines_.begin(), lines_.end(), [&](auto const& l) {
            return l.find(needle) != std::string::npos;
        });
    }

    void write(std::string_view line) noexcept override {
        std::lock_guard<std::mutex> lk{m_};
        lines_.emplace_back(line);
    }

private:
    std::vector<std::string> lines_;
    std::mutex m_;
};

ddcs::common::Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b.fill(std::byte{seed});
    return ddcs::common::Uuid{b};
}

// 단일 스레드로 양 reactor를 번갈아 펌프하며 pred를 기다린다.
template <typename Pred>
bool pump_until(ddcs::ctrl::Controller& c, ddcs::agent::Agent& a, Pred pred, int max_iter = 400) {
    for (int i = 0; i < max_iter; ++i) {
        if (pred()) {
            return true;
        }
        c.run_once(std::chrono::milliseconds{2});
        a.run_once(std::chrono::milliseconds{2});
    }
    return pred();
}

std::unique_ptr<ddcs::ctrl::Controller>
make_controller(CaptureSink& sink, std::filesystem::path policy_path = {}) {
    ddcs::ctrl::Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.liveness_timeout = std::chrono::milliseconds{300};
    cfg.sweep_interval = std::chrono::milliseconds{20}; // command/liveness/policy sweep을 촘촘히
    if (!policy_path.empty()) {
        cfg.policy_path = std::move(policy_path);
    }
    cfg.log_level = ddcs::logger::Level::debug;
    cfg.log_sink = &sink;
    return std::make_unique<ddcs::ctrl::Controller>(cfg);
}

std::unique_ptr<ddcs::agent::Agent>
make_agent(CaptureSink& sink, std::uint16_t port, std::uint8_t uuid_seed, std::string group = {}) {
    ddcs::agent::Agent::Config cfg{};
    cfg.controller_host = "127.0.0.1";
    cfg.controller_port = port;
    cfg.device = std::make_unique<ddcs::agent::domain::DummyDevice>(make_uuid(uuid_seed));
    cfg.session.heartbeat = std::chrono::milliseconds{50};
    cfg.session.status_update = std::chrono::milliseconds{50};
    cfg.session.register_timeout = std::chrono::milliseconds{500};
    cfg.session.group = std::move(group); // 정책 타깃팅용 그룹 선언
    cfg.log_level = ddcs::logger::Level::debug;
    cfg.log_sink = &sink;
    return std::make_unique<ddcs::agent::Agent>(std::move(cfg));
}

// 단일 그룹 정책을 temp 파일로 떨군다. low_load 위(>0)라 load=0인 DummyDevice는 idle로 떨어진다.
std::filesystem::path write_idle_policy(std::string_view group) {
    auto const path = std::filesystem::temp_directory_path() / "ddcs_e2e_policy.json";
    std::ofstream f{path};
    f << R"({"policy":{"groups":{")" << group
      << R"(":{"high_load":10.0,"low_load":5.0,"high_load_mode":"safe","low_load_mode":"performance"}}}})";
    return path;
}

} // namespace

TEST(AgentControllerE2eTest, AgentRegistersAndReachesActive) {
    CaptureSink sink;
    auto controller = make_controller(sink);
    controller->start();
    auto agent = make_agent(sink, controller->port(), 0xab);
    agent->start();

    bool const active =
        pump_until(*controller, *agent, [&] { return agent->session().state() == State::active; });

    ASSERT_TRUE(active);
    EXPECT_TRUE(sink.contains(R"("event":"session.registered")"));       // controller가 등록 확정
    EXPECT_TRUE(sink.contains(R"("event":"agent.session.registered")")); // agent가 응답 수신
}

TEST(AgentControllerE2eTest, HeartbeatKeepsAgentAliveOverTime) {
    CaptureSink sink;
    auto controller = make_controller(sink);
    controller->start();
    auto agent = make_agent(sink, controller->port(), 0xcd);
    agent->start();

    ASSERT_TRUE(pump_until(*controller, *agent, [&] {
        return agent->session().state() == State::active;
    }));

    // ~500ms 펌프 - heartbeat(50ms)가 controller liveness(300ms)를 계속 리셋
    auto const start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds{500}) {
        controller->run_once(std::chrono::milliseconds{2});
        agent->run_once(std::chrono::milliseconds{2});
    }

    EXPECT_EQ(agent->session().state(), State::active);
    EXPECT_FALSE(sink.contains(R"("event":"session.liveness_timeout")")); // 축출 안 됨
}

// 정책 경로로 c->a Command가 왕복한다(operator API 없음 - load 임계 전환이 명령을 낸다).
// load=0인 device가 low_load 밴드 아래라 controller가 idle_mode SetMode를 발신 -> agent 적용 ->
// outcome 회신.
TEST(AgentControllerE2eTest, PolicyCommandRoundTrips) {
    CaptureSink sink;
    auto const policy = write_idle_policy("edge");
    auto controller = make_controller(sink, policy);
    controller->start();
    auto agent = make_agent(sink, controller->port(), 0xef, "edge");
    agent->start();

    bool const round_trip = pump_until(*controller, *agent, [&] {
        return sink.contains(R"("event":"agent.session.cmd.applied")") // agent가 명령 적용
               && sink.contains(R"("event":"command.outcome")");       // controller가 outcome 상관
    });

    EXPECT_TRUE(round_trip);
    EXPECT_EQ(agent->session().state(), State::active); // 명령 후에도 세션 유지
}

// controller가 사라지면 agent가 backoff 후 같은 포트의 새 controller로 자동 재접속/재등록
TEST(AgentControllerE2eTest, AgentReconnectsAfterControllerDrop) {
    CaptureSink sink;
    auto controller = make_controller(sink);
    controller->start();
    auto const port = controller->port();
    auto agent = make_agent(sink, port, 0x5a);
    agent->start();

    ASSERT_TRUE(pump_until(*controller, *agent, [&] {
        return agent->session().state() == State::active;
    }));

    // controller 파괴 -> agent 연결 끊김(FIN)
    controller.reset();
    for (int i = 0; i < 100 && agent->session().state() == State::active; ++i) {
        agent->run_once(std::chrono::milliseconds{2});
    }
    EXPECT_NE(agent->session().state(), State::active); // 끊김 감지 -> idle

    // 같은 포트로 새 controller (Acceptor SO_REUSEADDR)
    ddcs::ctrl::Controller::Config ccfg{};
    ccfg.listen_port = port;
    ccfg.liveness_timeout = std::chrono::milliseconds{300};
    ccfg.log_level = ddcs::logger::Level::debug;
    ccfg.log_sink = &sink;
    auto controller2 = std::make_unique<ddcs::ctrl::Controller>(ccfg);
    controller2->start();

    // backoff(첫 지연 ~1s) 후 재접속/재등록 -> 다시 active. 넉넉히 펌프
    bool const reactive = pump_until(
        *controller2, *agent, [&] { return agent->session().state() == State::active; }, 1500
    );
    EXPECT_TRUE(reactive);
}
