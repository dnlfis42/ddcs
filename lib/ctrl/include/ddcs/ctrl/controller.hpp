#pragma once

#include "ddcs/common/clock.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/agent/register_service.hpp"
#include "ddcs/ctrl/app/agent/status_service.hpp"
#include "ddcs/ctrl/app/metrics/metrics_service.hpp"
#include "ddcs/ctrl/app/ops/operator_service.hpp"
#include "ddcs/ctrl/app/policy/policy_service.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/transport/dispatcher.hpp"
#include "ddcs/ctrl/app/transport/liveness_monitor.hpp"
#include "ddcs/ctrl/domain/agent/agent_registry.hpp"
#include "ddcs/ctrl/infra/metrics/server.hpp"
#include "ddcs/ctrl/infra/transport/acceptor.hpp"
#include "ddcs/ctrl/infra/transport/connection_coordinator.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"
#include "ddcs/logger/log.hpp"

#include <chrono>
#include <filesystem>
#include <optional>

#include <cstdint>

namespace ddcs::ctrl {

// Controller 조립 루트: io(Reactor) + infra(Coordinator/Acceptor) + app(Session/Agent/Command/Dispatcher)
// + domain(AgentRegistry)을 한데 묶는다. 외부(main, 통합 테스트)는 이 클래스만 다룬다.
// CommandService.sweep() 의 주기 구동을 위해 io::TimerHandler 를 구현한다.
class Controller : public io::TimerHandler {
public:
    struct Config {
        std::uint16_t listen_port{0}; // 0 = ephemeral
        int accept_backlog{128};
        // nullopt = metrics 엔드포인트 비활성. 값이 있으면 그 포트로 바인드(0 = ephemeral).
        std::optional<std::uint16_t> metrics_port{};
        std::chrono::nanoseconds liveness_timeout{std::chrono::seconds{3}};
        std::chrono::nanoseconds command_timeout{std::chrono::seconds{5}};
        int command_max_attempts{3}; // 부분실패 재시도(1 = 재시도 없음)
        std::chrono::nanoseconds command_backoff_base{std::chrono::milliseconds{500}}; // 지수 backoff 기준
        std::chrono::nanoseconds sweep_interval{std::chrono::seconds{1}}; // command/liveness/policy sweep 주기
        // nullopt = 정책 비활성. 값이 있으면 부팅 시 그 policy.json 을 load-once.
        std::optional<std::filesystem::path> policy_path{};

        logger::Level log_level{logger::Level::Info};
        // nullptr 이면 내부 기본 StdoutSink 설치. 직접 주입 시 그 sink 우선.
        logger::Sink* log_sink{nullptr};
    };

    explicit Controller(Config cfg);
    ~Controller() override;

    Controller(Controller const&) = delete;
    Controller& operator=(Controller const&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    void start();                                     // listen 개시 + sweep 타이머 예약
    void run();                                       // 이벤트 루프 (블로킹)
    void run_once(std::chrono::milliseconds timeout); // 1회 디스패치
    void stop();                                      // 멱등

    std::uint16_t port() const { return acceptor_.port(); }
    // metrics 엔드포인트 바인드 포트. 비활성이면 0.
    std::uint16_t metrics_port() const { return metrics_server_ ? metrics_server_->port() : 0; }

    // operator API (driving): agent 에게 SetMode 명령 발신. 반환 command_id(미지/미연결 0).
    std::uint64_t set_mode(common::Uuid const& agent_uuid, device::Mode mode) {
        return operator_service_.set_mode(agent_uuid, mode);
    }

public: // io::TimerHandler - sweep 타이머 만료
    void on_timer(io::TimerId id) override;

private:
    void schedule_sweep();
    void load_policy(); // policy.json load-once (start 에서). 파일/파싱 실패는 WARN 후 빈 정책.

    logger::StdoutSink default_sink_;
    common::SystemClock clock_;
    Config cfg_;

    io::Reactor reactor_;
    infra::transport::ConnectionCoordinator coordinator_;
    infra::transport::Acceptor acceptor_;

    app::session::SessionRegistry sessions_;
    domain::agent::AgentRegistry registry_;
    app::agent::RegisterService registrar_;
    app::agent::StatusService status_;
    app::agent::CommandService commands_;
    app::transport::Dispatcher dispatcher_;
    app::ops::OperatorService operator_service_;
    app::policy::PolicyService policy_;
    app::transport::LivenessMonitor liveness_;
    app::metrics::MetricsService metrics_service_;
    // reactor 의 2nd guest. Config.metrics_port 있을 때만 start() 에서 emplace.
    // metrics_service_ 뒤에 선언 -> 먼저 소멸(Inbound& 참조가 dangling 되지 않도록).
    std::optional<infra::metrics::Server> metrics_server_;

    io::TimerId sweep_timer_{};
};

} // namespace ddcs::ctrl
