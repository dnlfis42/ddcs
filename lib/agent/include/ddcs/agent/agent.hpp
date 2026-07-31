#pragma once

#include "ddcs/agent/app/session/session_service.hpp"
#include "ddcs/agent/domain/device.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace ddcs::agent {

// Agent 조립 루트 facade. transport/session 내부를 가린다.
// 외부(main, 통합 테스트)는 이 Agent와 주입할 Device만 다룬다.
class Agent {
public:
    // 멤버 순서는 agent.json 절 순서(transport/session)를 따른다. log 절은 프로세스 로깅
    // 부트스트랩이라 main 몫이다.
    struct Config {
        std::string controller_host = "127.0.0.1";
        std::uint16_t controller_port = 8080;
        // per-connection rx ring 용량(byte). frame 최대 크기 미만이면 조립 시 하한으로 보정
        std::size_t rx_buffer_size = 1 << 12;
        // 재연결 backoff (base * 2^attempt 를 max로 cap)
        std::chrono::nanoseconds reconnect_base_delay = std::chrono::seconds{1};
        std::chrono::nanoseconds reconnect_max_delay = std::chrono::seconds{30};

        app::session::SessionService::Config session;
    };

    // device는 필수 의존이라 인자로 강제한다. 로깅은 프로세스 관심사라 여기서 다루지 않는다.
    // 호출자(main, 테스트)가 전역 로거를 먼저 세워 두면 조립 중 경고부터 그 sink에 잡힌다.
    Agent(Config cfg, std::unique_ptr<domain::Device> device);
    ~Agent();

    Agent(Agent const&) = delete;
    Agent& operator=(Agent const&) = delete;
    Agent(Agent&&) = delete;
    Agent& operator=(Agent&&) = delete;

    void start();                                     // 첫 connect 개시
    void run();                                       // 이벤트 루프 (블로킹)
    void run_once(std::chrono::milliseconds timeout); // 1회 디스패치
    void stop();                                      // 멱등

    // 테스트/관측 노출
    app::session::SessionService& session() noexcept;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::agent
