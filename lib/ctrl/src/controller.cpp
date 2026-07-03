#include "ddcs/ctrl/controller.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/policy_service.hpp"
#include "ddcs/ctrl/app/device/registration_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/app/metrics/metrics_service.hpp"
#include "ddcs/ctrl/app/metrics/sweep_stats.hpp"
#include "ddcs/ctrl/app/session/command_sender.hpp"
#include "ddcs/ctrl/app/session/device_roster.hpp"
#include "ddcs/ctrl/app/session/handshake_monitor.hpp"
#include "ddcs/ctrl/app/session/liveness_monitor.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/session/session_service.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/infra/prometheus/server.hpp"
#include "ddcs/ctrl/infra/transport/server.hpp"
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

    std::uint16_t port() const {
        return transport_server_.port();
    }

    std::uint16_t metrics_port() const {
        return prometheus_server_ ? prometheus_server_->port() : 0;
    }

private:
    void on_expired(io::TimerId id) override;
    void schedule_sweep();
    void load_policy();          // policy load (start + SIGHUP reload). 실패는 WARN 후 옛 정책 유지
    void handle_signal(int sig); // SIGHUP=정책 핫리로드 / SIGINT,SIGTERM=stop

    logger::StdoutSink default_sink_;
    common::SteadyClock clock_;
    Config cfg_;

    io::Reactor reactor_;
    io::SignalSource signal_source_;
    io::TimerScheduler timer_scheduler_;

    // sender()/disconnector() 제공이라 의존자보다 먼저 선언
    infra::transport::Server transport_server_;

    app::session::SessionRegistry sessions_;
    domain::DeviceRegistry devices_;
    app::session::CommandSender command_sender_;
    app::device::CommandService commands_;
    app::device::RegistrationService registrar_;
    app::device::StatusService status_;
    app::session::DeviceRoster roster_;
    app::device::PolicyService policy_;
    app::session::HandshakeMonitor handshake_monitor_;
    app::session::LivenessMonitor liveness_monitor_;
    // transport_server_의 ConnectionListener + MessageReceiver
    app::session::SessionService session_service_;
    app::metrics::SweepStats sweep_stats_; // metrics_service_ 보다 먼저 선언(참조 유효)
    app::metrics::MetricsService metrics_service_;
    // reactor의 2nd guest. Config.metrics_port 있을 때만 start()에서 emplace
    // metrics_service_ 뒤에 선언해 먼저 소멸 (MetricsSource& 참조가 dangling 되지 않도록)
    std::optional<infra::prometheus::Server> prometheus_server_;

    io::TimerId sweep_timer_;
};

Controller::Impl::Impl(Config cfg)
    : cfg_(std::move(cfg)),
      signal_source_(reactor_, {SIGINT, SIGTERM, SIGHUP}, [this](int sig) { handle_signal(sig); }),
      timer_scheduler_(reactor_),
      transport_server_(reactor_, cfg_.listen_port, cfg_.accept_backlog),
      command_sender_(sessions_, transport_server_.sender()),
      commands_(
          command_sender_, cfg_.command_timeout, cfg_.command_max_attempts,
          cfg_.command_backoff_base
      ),
      registrar_(devices_),
      status_(devices_),
      roster_(sessions_),
      policy_(roster_, devices_, commands_),
      handshake_monitor_(sessions_, transport_server_.disconnector(), cfg_.handshake_timeout),
      liveness_monitor_(sessions_, transport_server_.disconnector(), cfg_.liveness_timeout),
      session_service_(
          sessions_, transport_server_.disconnector(), transport_server_.sender(), clock_,
          registrar_, status_, commands_, policy_, policy_.policy()
      ),
      metrics_service_(
          sessions_, devices_, roster_, commands_, liveness_monitor_, handshake_monitor_,
          policy_.policy(), sweep_stats_
      ) {
    auto& lg = logger::Logger::instance();
    lg.set_level(cfg_.log_level);
    lg.set_sink(cfg_.log_sink != nullptr ? *cfg_.log_sink : default_sink_);
}

Controller::Impl::~Impl() {
    stop();
    // transport Server dtor가 on_disconnected를 notify하므로,
    // SessionService(listener/receiver)가 살아있는 지금 명시적으로 닫는다.
    // CAUTION: 멤버 소멸은 역순이라 transport_server_가 session_service_보다 늦게 소멸한다.
    //          생성 의존 때문에 선언 순서 고정
    transport_server_.close();
    // default_sink_가 곧 파괴되므로, 전역 Logger가 그것을 가리키면 떼어내 dangling을 막는다.
    logger::Logger::instance().clear_sink(default_sink_);
}

void Controller::Impl::start() {
    signal_source_.start();
    timer_scheduler_.start();
    if (!transport_server_.init(session_service_, session_service_)) {
        throw std::runtime_error{"transport server init failed"};
    }
    if (!transport_server_.start()) {
        throw std::runtime_error{"transport server start failed"};
    }
    if (cfg_.metrics_port) {
        // 스크레이프는 저빈도라 작은 backlog로 충분
        constexpr int metrics_backlog = 16;
        prometheus_server_.emplace(reactor_, metrics_service_, *cfg_.metrics_port, metrics_backlog);
        if (!prometheus_server_->init() || !prometheus_server_->start()) {
            throw std::runtime_error{"prometheus server start failed"};
        }
    }
    load_policy();
    schedule_sweep();
}

void Controller::Impl::run() {
    reactor_.run();
}

void Controller::Impl::run_once(std::chrono::milliseconds timeout) {
    reactor_.run_once(timeout);
}

void Controller::Impl::stop() {
    timer_scheduler_.stop();
    signal_source_.stop();
    reactor_.stop();
}

void Controller::Impl::on_expired(io::TimerId /*id*/) {
    // 이 핸들러로 오는 타이머는 주기 sweep 뿐이다. 한 tick의 now를 모든 호출에 공유한다.
    auto const now = clock_.now();
    commands_.sweep(now);
    handshake_monitor_.sweep(now);           // 등록 미완 시한 초과 시 disconnect
    liveness_monitor_.sweep(now);            // active 침묵 시 evict(close)
    policy_.evaluate(now);                   // 그룹 load 집계 후 임계 전환 시 SetMode 발신
    sweep_stats_.record(clock_.now() - now); // tick 작업 소요(schedule 제외) 기록
    schedule_sweep();                        // 주기 재무장
}

void Controller::Impl::schedule_sweep() {
    sweep_timer_ = timer_scheduler_.schedule(cfg_.sweep_interval, *this);
}

void Controller::Impl::load_policy() {
    if (!cfg_.policy_path) {
        return; // 정책 비활성 (빈 정책이면 evaluate no-op)
    }
    auto const& path = *cfg_.policy_path;
    std::ifstream file{path};
    if (!file) {
        LOG_WARN("policy.load.open_fail", logger::kv("path", path.string()));
        return;
    }
    std::string const text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    auto const json = json::parse(text);
    if (!json) {
        LOG_WARN("policy.load.parse_fail", logger::kv("path", path.string()));
        return;
    }
    // 정책은 controller 설정 파일에 인라인된 "policy" 객체다.
    auto const* policy_node = json->find("policy");
    if (policy_node == nullptr) {
        LOG_WARN("policy.load.absent", logger::kv("path", path.string()));
        return; // 정책 없음 = 빈 정책 (evaluate no-op)
    }
    auto policy = app::device::parse_policy(*policy_node);
    if (!policy) {
        LOG_WARN("policy.load.invalid", logger::kv("path", path.string()));
        return;
    }
    LOG_INFO(
        "policy.load", logger::kv("path", path.string()), logger::kv("groups", policy->size())
    );
    policy_.set_policy(std::move(*policy));
}

void Controller::Impl::handle_signal(int sig) {
    if (sig == SIGHUP) {
        // 정책만 핫리로드(재적용). 다른 설정(포트/타임아웃)은 부팅 시 고정한다.
        // malformed/없음이면 load_policy가 옛 정책을 유지한다(validate-before-apply).
        // set_policy가 발신 belief(commanded)를 비우므로 다음 sweep이 새 정책으로 재명령한다
        // (regime/thermal 히스테리시스 latch는 reload를 넘어 보존된다).
        LOG_INFO("policy.reload");
        load_policy();
        return;
    }
    stop(); // SIGINT / SIGTERM
}

Controller::Controller(Config cfg)
    : impl_(std::make_unique<Impl>(std::move(cfg))) {}

Controller::~Controller() = default;

void Controller::start() {
    impl_->start();
}

void Controller::run() {
    impl_->run();
}

void Controller::run_once(std::chrono::milliseconds timeout) {
    impl_->run_once(timeout);
}

void Controller::stop() {
    impl_->stop();
}

std::uint16_t Controller::port() const {
    return impl_->port();
}

std::uint16_t Controller::metrics_port() const {
    return impl_->metrics_port();
}

} // namespace ddcs::ctrl
