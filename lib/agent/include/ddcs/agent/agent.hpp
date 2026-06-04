#pragma once

#include "ddcs/agent/app/session_service.hpp"
#include "ddcs/agent/domain/device.hpp"
#include "ddcs/agent/infra/connector.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/runtime/reactor.hpp"
#include "ddcs/runtime/signal_source.hpp"
#include "ddcs/logger/log.hpp"

#include <chrono>
#include <memory>
#include <string>

#include <cstdint>

namespace ddcs::agent {

// Agent 조립 루트: runtime(Reactor/SignalSource) + infra(Connector) + app(SessionService) + domain(Device)를 묶는다.
// 외부(main, 통합 테스트)는 이 클래스만 다룬다.
class Agent {
public:
    struct Config {
        std::string controller_host{"127.0.0.1"};
        std::uint16_t controller_port{8080};
        common::Uuid agent_uuid{}; // ctor 호출 전 채워서 줄 것
        // uuid 가 부팅마다 새로 생성된 random 이면 true - controller 의 kick-old/identity persistence 가
        // 의미를 잃으므로 ctor 가 LOG_WARN 으로 운영자에게 알린다.
        bool agent_uuid_is_ephemeral{false};
        std::unique_ptr<domain::Device> device; // 필수

        app::SessionService::Config session{};

        logger::Level log_level{logger::Level::Info};
        logger::Sink* log_sink{nullptr}; // nullptr -> 내부 StdoutSink
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
    app::SessionService& session() noexcept { return session_; }

private:
    logger::StdoutSink default_sink_;
    std::unique_ptr<domain::Device> device_;
    runtime::Reactor reactor_;
    runtime::SignalSource signal_source_;
    infra::Connector connector_;
    app::SessionService session_;
};

} // namespace ddcs::agent
