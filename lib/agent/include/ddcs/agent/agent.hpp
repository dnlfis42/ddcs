#pragma once

#include "ddcs/agent/app/session/session_service.hpp"
#include "ddcs/agent/domain/device.hpp"
#include "ddcs/logger/log.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace ddcs::agent {

// Agent 조립 루트 facade
// - 외부(main, 통합 테스트)는 이 Agent와 주입할 Device(Config.device)만 다룬다.
// - transport/session 내부는 facade가 가린다.
class Agent {
public:
    struct Config {
        std::string controller_host = "127.0.0.1";
        std::uint16_t controller_port = 8080;
        // 재연결 backoff (base * 2^attempt 를 max로 cap). 미지정이면 1s/30s.
        std::chrono::nanoseconds reconnect_base_delay = std::chrono::seconds{1};
        std::chrono::nanoseconds reconnect_max_delay = std::chrono::seconds{30};
        // device 신원(Device::id())이 부팅마다 새로 생성된 random이면 true이고,
        // controller의 kick-old/identity persistence가 의미를 잃으므로
        // ctor가 LOG_WARN으로 운영자에게 알린다.
        bool device_id_is_ephemeral = false;
        std::unique_ptr<domain::Device> device; // 필수

        app::session::SessionService::Config session;

        logger::Level log_level = logger::Level::info;
        logger::Sink* log_sink = nullptr; // nullptr이면 내부 StdoutSink
    };

    explicit Agent(Config cfg);
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
