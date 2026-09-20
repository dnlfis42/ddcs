#pragma once

#include "ddcs/agent/agent.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <vector>

namespace ddcs::agent {

// 여러 Device의 Agent 세션을 하나의 이벤트 루프로 실행하는 테스트용 조립 루트.
// 각 Device의 UUID는 서로 달라야 한다. 모든 메서드는 같은 스레드에서 호출한다.
class AgentFleet {
public:
    struct Config {
        Agent::Config agent;
        std::chrono::milliseconds start_interval{1};
    };

    AgentFleet(Config cfg, std::vector<std::unique_ptr<domain::Device>> devices);
    ~AgentFleet();

    AgentFleet(AgentFleet const&) = delete;
    AgentFleet(AgentFleet&&) = delete;
    AgentFleet& operator=(AgentFleet const&) = delete;
    AgentFleet& operator=(AgentFleet&&) = delete;

    void start();
    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop(); // 멱등. 연결을 모두 닫는다. stop 이후 재시작은 허용하지 않는다.

    std::size_t size() const noexcept;
    std::size_t active_count() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::agent
