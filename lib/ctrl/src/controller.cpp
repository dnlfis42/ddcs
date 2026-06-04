#include "ddcs/ctrl/controller.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/command_service.hpp"
#include "ddcs/ctrl/app/agent/register_service.hpp"
#include "ddcs/ctrl/app/agent/status_service.hpp"
#include "ddcs/ctrl/app/metrics/metrics_service.hpp"
#include "ddcs/ctrl/app/ops/operator_service.hpp"
#include "ddcs/ctrl/app/policy/policy_service.hpp"
#include "ddcs/ctrl/app/session/liveness_monitor.hpp"
#include "ddcs/ctrl/app/session/session_manager.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/infra/metrics/server.hpp"
#include "ddcs/ctrl/infra/transport/acceptor.hpp"
#include "ddcs/ctrl/infra/transport/connection_coordinator.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"
#include "ddcs/json/value.hpp"

#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ddcs::ctrl {

class Controller::Impl final : public io::TimerHandler {
public:
    explicit Impl(Config cfg);
    ~Impl() override;

    Impl(Impl const&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void start();
    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop();

    std::uint16_t port() const { return acceptor_.port(); }
    std::uint16_t metrics_port() const { return metrics_server_ ? metrics_server_->port() : 0; }
    std::uint64_t set_mode(common::Uuid const& agent_uuid, device::Mode mode) {
        return operator_service_.set_mode(agent_uuid, mode);
    }

private:
    void on_timer(io::TimerId id) override;
    void schedule_sweep();
    void load_policy(); // policy.json load-once (start에서). 파일/파싱 실패는 WARN 후 빈 정책.

    logger::StdoutSink default_sink_;
    common::SystemClock clock_;
    Config cfg_;

    io::Reactor reactor_;
    infra::transport::ConnectionCoordinator coordinator_;
    infra::transport::Acceptor acceptor_;

    app::session::SessionRegistry sessions_;
    domain::DeviceRegistry registry_;
    app::agent::RegisterService registrar_;
    app::agent::StatusService status_;
    app::agent::CommandService commands_;
    app::session::SessionManager session_manager_;
    app::ops::OperatorService operator_service_;
    app::policy::PolicyService policy_;
    app::session::LivenessMonitor liveness_;
    app::metrics::MetricsService metrics_service_;
    // reactor의 2nd guest. Config.metrics_port 있을 때만 start()에서 emplace.
    // metrics_service_ 뒤에 선언 -> 먼저 소멸(Inbound& 참조가 dangling 되지 않도록).
    std::optional<infra::metrics::Server> metrics_server_;

    io::TimerId sweep_timer_{};
};

Controller::Impl::Impl(Config cfg)
    : cfg_{cfg}, coordinator_{reactor_}, acceptor_{reactor_, coordinator_, cfg.listen_port, cfg.accept_backlog},
      registrar_{registry_, coordinator_}, status_{sessions_, registry_},
      commands_{
          sessions_, coordinator_, clock_, cfg.command_timeout, cfg.command_max_attempts, cfg.command_backoff_base
      },
      session_manager_{sessions_, registrar_, status_, commands_, coordinator_, clock_},
      operator_service_{registry_, commands_}, policy_{sessions_, registry_, operator_service_},
      liveness_{sessions_, coordinator_, clock_, cfg.liveness_timeout},
      metrics_service_{sessions_, registry_, commands_, session_manager_, liveness_} {
    auto& lg = logger::Logger::instance();
    lg.set_level(cfg.log_level);
    lg.set_sink(cfg.log_sink != nullptr ? *cfg.log_sink : default_sink_);

    coordinator_.init(session_manager_); // inbound 포트 주입
}

Controller::Impl::~Impl() {
    stop();
    // 멤버 dtor 역순: session_manager_ -> ... -> coordinator_ -> reactor_ (reactor 가 마지막에 소멸).
}

void Controller::Impl::start() {
    acceptor_.start();
    if (cfg_.metrics_port) {
        constexpr int metrics_backlog{16}; // 스크레이프는 저빈도 - 작은 backlog 로 충분
        metrics_server_.emplace(reactor_, metrics_service_, *cfg_.metrics_port, metrics_backlog);
        metrics_server_->start();
    }
    load_policy();
    schedule_sweep();
}

void Controller::Impl::run() { reactor_.run(); }

void Controller::Impl::run_once(std::chrono::milliseconds timeout) { reactor_.run_once(timeout); }

void Controller::Impl::stop() { reactor_.stop(); }

void Controller::Impl::on_timer(io::TimerId /*id*/) {
    // 이 핸들러로 오는 타이머는 주기 sweep 뿐이다.
    commands_.sweep();
    liveness_.sweep();                // active 세션 침묵 -> evict(close)
    policy_.evaluate();               // 그룹 load 집계 -> 임계 전환 시 SetMode 발신
    coordinator_.close_connections(); // sweep/evaluate 의 close 는 entry-point 밖 -> 여기서 reap 구동
    schedule_sweep();                 // 주기 재무장
}

void Controller::Impl::schedule_sweep() { sweep_timer_ = reactor_.schedule(cfg_.sweep_interval, this); }

void Controller::Impl::load_policy() {
    if (!cfg_.policy_path) {
        return; // 정책 비활성(빈 정책 -> evaluate no-op)
    }
    auto const& path = *cfg_.policy_path;
    std::ifstream file{path};
    if (!file) {
        LOG_WARN("policy.load.open_fail", logger::kv("path", path.string()));
        return;
    }
    std::string const text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    auto const json = json::Value::parse(text);
    if (!json) {
        LOG_WARN("policy.load.parse_fail", logger::kv("path", path.string()));
        return;
    }
    auto policy = app::policy::parse_policy(*json);
    if (!policy) {
        LOG_WARN("policy.load.invalid", logger::kv("path", path.string()));
        return;
    }
    LOG_INFO("policy.load", logger::kv("path", path.string()), logger::kv("groups", policy->size()));
    policy_.set_policy(std::move(*policy));
}

Controller::Controller(Config cfg) : impl_{std::make_unique<Impl>(std::move(cfg))} {}

Controller::~Controller() = default;

void Controller::start() { impl_->start(); }

void Controller::run() { impl_->run(); }

void Controller::run_once(std::chrono::milliseconds timeout) { impl_->run_once(timeout); }

void Controller::stop() { impl_->stop(); }

std::uint16_t Controller::port() const { return impl_->port(); }

std::uint16_t Controller::metrics_port() const { return impl_->metrics_port(); }

std::uint64_t Controller::set_mode(common::Uuid const& agent_uuid, device::Mode mode) {
    return impl_->set_mode(agent_uuid, mode);
}

} // namespace ddcs::ctrl
