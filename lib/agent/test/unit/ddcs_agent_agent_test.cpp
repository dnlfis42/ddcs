#include "ddcs/agent/agent.hpp"

#include "ddcs/agent/app/session_service.hpp"
#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/logger/log.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

namespace {

// 조립 루트 스모크: 구성, start, run_once, stop 순서가 무사한지
TEST(AgentTest, AssemblesStartsAndDispatchesWithoutController) {
    ddcs::agent::Agent::Config cfg{};
    cfg.controller_host = "127.0.0.1";
    cfg.controller_port = 65000; // 리스너 없으면 connect refused되어 backoff
    cfg.device = std::make_unique<ddcs::agent::domain::DummyDevice>();
    cfg.log_level = ddcs::logger::Level::Warn;

    ddcs::agent::Agent agent{std::move(cfg)};
    agent.start();
    agent.run_once(std::chrono::milliseconds{10});
    agent.run_once(std::chrono::milliseconds{10});

    // 컨트롤러 없으면 등록 못 함. idle 유지 (크래시 없이)
    EXPECT_EQ(agent.session().state(), ddcs::agent::app::SessionService::State::idle);
    agent.stop();
}

} // namespace
