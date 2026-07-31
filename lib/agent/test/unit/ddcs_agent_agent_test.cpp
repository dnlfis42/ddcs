#include "ddcs/agent/agent.hpp"

#include "ddcs/agent/app/session/session_service.hpp"
#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/logger/log.hpp"

#include <chrono>
#include <memory>

#include <gtest/gtest.h>

namespace {

// 조립 루트 스모크: 구성, start, run_once, stop 순서가 무사한지
TEST(AgentTest, AssemblesStartsAndDispatchesWithoutController) {
    // 프로세스 로깅 부트스트랩은 호출자 몫이다(main과 동일 수순)
    ddcs::logger::StdoutSink sink;
    auto& lg = ddcs::logger::Logger::instance();
    lg.set_level(ddcs::logger::Level::warn);
    lg.set_sink(sink);

    ddcs::agent::Agent::Config cfg{};
    cfg.controller_host = "127.0.0.1";
    cfg.controller_port = 65000; // 리스너 없으면 connect refused되어 backoff

    ddcs::agent::Agent agent{std::move(cfg), std::make_unique<ddcs::agent::domain::DummyDevice>()};
    agent.start();
    agent.run_once(std::chrono::milliseconds{10});
    agent.run_once(std::chrono::milliseconds{10});

    // 컨트롤러 없으면 등록 못 함. idle 유지 (크래시 없이)
    EXPECT_EQ(agent.session().state(), ddcs::agent::app::session::SessionService::State::idle);
    agent.stop();
}

} // namespace
