#include "ddcs/ctrl/controller.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/agent/agent_registry.hpp"
#include "ddcs/ctrl/app/agent/agent_service.hpp"
#include "ddcs/ctrl/app/agent/command_sender.hpp"
#include "ddcs/ctrl/app/agent/device_roster.hpp"
#include "ddcs/ctrl/app/agent/handshake_monitor.hpp"
#include "ddcs/ctrl/app/agent/liveness_monitor.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/policy_service.hpp"
#include "ddcs/ctrl/app/device/register_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/app/metrics/metrics_service.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/infra/dacp/server.hpp"
#include "ddcs/ctrl/infra/prometheus/server.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/signal_source.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"
#include "ddcs/io/timer_scheduler.hpp"
#include "ddcs/json/value.hpp"

#include <csignal>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
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

    std::uint16_t port() const { return dacp_server_.port(); }
    std::uint16_t metrics_port() const { return prometheus_server_ ? prometheus_server_->port() : 0; }

private:
    void on_expired(io::TimerId id) override;
    void schedule_sweep();
    void load_policy(); // policy.json load-once (start에서). 파일/파싱 실패는 WARN 후 빈 정책.

    logger::StdoutSink default_sink_;
    common::SteadyClock clock_;
    Config cfg_;

    io::Reactor reactor_;
    io::SignalSource signal_source_;
    io::TimerScheduler timer_scheduler_;
    infra::dacp::Server dacp_server_; // sender()/disconnector() 제공 -> 의존자보다 먼저 선언

    app::agent::AgentRegistry agents_;
    domain::DeviceRegistry devices_;
    app::agent::CommandSender command_sender_;
    app::device::CommandService commands_;
    app::device::RegisterService registrar_;
    app::device::StatusService status_;
    app::agent::DeviceRoster roster_;
    app::device::PolicyService policy_;
    app::agent::HandshakeMonitor handshake_monitor_;
    app::agent::LivenessMonitor liveness_monitor_;
    app::agent::AgentService agent_service_; // dacp_server_의 ConnectionObserver
    app::metrics::MetricsService metrics_service_;
    // reactor의 2nd guest. Config.metrics_port 있을 때만 start()에서 emplace.
    // metrics_service_ 뒤에 선언 -> 먼저 소멸(MetricsSource& 참조가 dangling 되지 않도록).
    std::optional<infra::prometheus::Server> prometheus_server_;

    io::TimerId sweep_timer_{};
};

Controller::Impl::Impl(Config cfg)
    : cfg_{std::move(cfg)}, signal_source_{reactor_, {SIGINT, SIGTERM}, [this](int) { stop(); }},
      timer_scheduler_{reactor_}, dacp_server_{reactor_, cfg_.listen_port, cfg_.accept_backlog},
      command_sender_{agents_, dacp_server_.sender()},
      commands_{command_sender_, cfg_.command_timeout, cfg_.command_max_attempts, cfg_.command_backoff_base},
      registrar_{devices_}, status_{devices_}, roster_{agents_}, policy_{roster_, devices_, commands_},
      handshake_monitor_{agents_, dacp_server_.disconnector(), cfg_.handshake_timeout},
      liveness_monitor_{agents_, dacp_server_.disconnector(), cfg_.liveness_timeout},
      agent_service_{agents_,  dacp_server_.sender(), dacp_server_.disconnector(), clock_, registrar_, status_,
                     commands_},
      metrics_service_{agents_, devices_, commands_, liveness_monitor_, handshake_monitor_} {
    auto& lg = logger::Logger::instance();
    lg.set_level(cfg_.log_level);
    lg.set_sink(cfg_.log_sink != nullptr ? *cfg_.log_sink : default_sink_);
}

Controller::Impl::~Impl() {
    stop();
    // dacp Server dtor가 on_disconnected를 notify하므로, AgentService(observer)가 살아있는 지금 명시적으로 닫는다.
    // CAUTION: 멤버 소멸은 역순 - dacp_server_가 agent_service_보다 늦게 소멸한다(생성 의존 때문에 선언 순서 고정).
    dacp_server_.close();
}

void Controller::Impl::start() {
    signal_source_.start();
    timer_scheduler_.start();
    if (!dacp_server_.init(agent_service_)) {
        throw std::runtime_error{"dacp server init failed"};
    }
    if (!dacp_server_.start()) {
        throw std::runtime_error{"dacp server start failed"};
    }
    if (cfg_.metrics_port) {
        constexpr int metrics_backlog{16}; // 스크레이프는 저빈도 - 작은 backlog로 충분
        prometheus_server_.emplace(reactor_, metrics_service_, *cfg_.metrics_port, metrics_backlog);
        if (!prometheus_server_->init() || !prometheus_server_->start()) {
            throw std::runtime_error{"prometheus server start failed"};
        }
    }
    load_policy();
    schedule_sweep();
}

void Controller::Impl::run() { reactor_.run(); }

void Controller::Impl::run_once(std::chrono::milliseconds timeout) { reactor_.run_once(timeout); }

void Controller::Impl::stop() {
    timer_scheduler_.stop();
    signal_source_.stop();
    reactor_.stop();
}

void Controller::Impl::on_expired(io::TimerId /*id*/) {
    // 이 핸들러로 오는 타이머는 주기 sweep 뿐이다. 한 tick의 now를 모든 호출에 공유한다.
    auto const now = clock_.now();
    commands_.sweep(now);
    handshake_monitor_.sweep(now); // 등록 미완 시한 초과 -> disconnect
    liveness_monitor_.sweep(now);  // active 침묵 -> evict(close)
    policy_.evaluate(now);         // 그룹 load 집계 -> 임계 전환 시 SetMode 발신
    schedule_sweep();              // 주기 재무장
}

void Controller::Impl::schedule_sweep() { sweep_timer_ = timer_scheduler_.schedule(cfg_.sweep_interval, *this); }

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
    auto policy = app::device::parse_policy(*json);
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

} // namespace ddcs::ctrl
