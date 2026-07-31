#include "ddcs/ctrl/controller.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/policy_service.hpp"
#include "ddcs/ctrl/app/device/registration_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/app/metrics/duration_stats.hpp"
#include "ddcs/ctrl/app/metrics/metrics_service.hpp"
#include "ddcs/ctrl/app/session/command_sender.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/session/session_service.hpp"
#include "ddcs/ctrl/domain/device_registry.hpp"
#include "ddcs/ctrl/infra/prometheus/server.hpp"
#include "ddcs/ctrl/infra/transport/server.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/signal_source.hpp"
#include "ddcs/io/sys_result.hpp"
#include "ddcs/io/throw_errno.hpp"
#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_scheduler.hpp"
#include "ddcs/io/timer_token.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/logger/event.hpp"

#include <cassert>
#include <csignal>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ddcs::ctrl {

namespace {

// 부팅 실패를 main의 단일 catch로 승격한다. 원인이 있으면 errno 문장이 붙는다.
} // namespace

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

    std::uint16_t prometheus_port() const {
        return prometheus_server_ ? prometheus_server_->port() : 0;
    }

private:
    void on_expired(io::TimerToken id) override;
    void schedule_sweep();
    // policy load (부팅 + SIGHUP 재적재). 실패는 WARN 후 옛 정책 유지.
    // trigger 는 "boot" 또는 "reload" 로, policy.load* 줄이 그대로 싣는다.
    void load_policy(std::string_view trigger);
    void handle_signal(int sig); // SIGHUP=정책 핫리로드 / SIGINT,SIGTERM=stop

    common::SteadyClock clock_;
    Config cfg_;

    io::Reactor reactor_;
    io::SignalSource signal_source_;
    io::TimerScheduler timer_scheduler_;

    // sender()/disconnector() 제공이라 의존자보다 먼저 선언
    infra::transport::Server transport_server_;

    app::session::SessionRegistry session_registry_;
    domain::DeviceRegistry device_registry_;

    app::session::CommandSender command_sender_;
    app::device::CommandService command_service_;

    app::device::RegistrationService registration_service_;
    app::device::StatusService status_service_;
    app::device::PolicyService policy_service_;
    // transport_server_의 ConnectionListener + MessageReceiver. 시한 감시(sweep)도 소유
    app::session::SessionService session_service_;
    app::metrics::DurationStats sweep_stats_; // metrics_service_ 보다 먼저 선언(참조 유효)
    app::metrics::MetricsService metrics_service_;

    // reactor의 2nd guest. Config.prometheus_port 있을 때만 start()에서 emplace
    // metrics_service_ 뒤에 선언해 먼저 소멸 (MetricsSource& 참조가 dangling 되지 않도록)
    std::optional<infra::prometheus::Server> prometheus_server_;

    io::TimerToken sweep_timer_;
};

Controller::Impl::Impl(Config cfg)
    : cfg_(std::move(cfg)),
      signal_source_(reactor_, {SIGINT, SIGTERM, SIGHUP}, [this](int sig) { handle_signal(sig); }),
      timer_scheduler_(reactor_),
      transport_server_(reactor_, cfg_.listen_port, cfg_.accept_backlog, cfg_.rx_buffer_size),
      command_sender_(session_registry_, transport_server_.sender()),
      command_service_(
          command_sender_, cfg_.command_timeout, cfg_.command_max_attempts,
          cfg_.command_backoff_base
      ),
      registration_service_(device_registry_),
      status_service_(device_registry_),
      policy_service_(session_registry_, device_registry_, command_service_),
      session_service_(
          session_registry_, transport_server_.disconnector(), transport_server_.sender(), clock_,
          registration_service_, status_service_, command_service_, policy_service_,
          policy_service_.policy(), cfg_.handshake_timeout, cfg_.liveness_timeout
      ),
      metrics_service_(
          session_registry_, device_registry_, session_registry_, command_service_,
          session_service_, policy_service_.policy(), sweep_stats_
      ) {}

Controller::Impl::~Impl() {
    stop();
    // transport Server dtor가 on_disconnected를 notify하므로,
    // listener/receiver인 SessionService가 살아있는 지금 명시적으로 닫는다.
    // CAUTION: 선언은 생성 의존으로 고정이라 역순 소멸에서 transport_server_가 더 늦게 죽는다.
    transport_server_.close();
}

void Controller::Impl::start() {
    signal_source_.start();
    timer_scheduler_.start();
    if (auto const result = transport_server_.init(session_service_, session_service_); !result) {
        io::throw_boot_failure(result, "transport listen port " + std::to_string(cfg_.listen_port));
    }
    if (auto const result = transport_server_.start(); !result) {
        io::throw_boot_failure(result, "transport server start");
    }
    if (cfg_.prometheus_port) {
        // 스크레이프는 저빈도라 작은 backlog로 충분
        constexpr int metrics_backlog = 16;
        prometheus_server_.emplace(
            reactor_, metrics_service_, *cfg_.prometheus_port, metrics_backlog
        );
        if (auto const result = prometheus_server_->init(); !result) {
            io::throw_boot_failure(
                result, "prometheus listen port " + std::to_string(*cfg_.prometheus_port)
            );
        }
        if (auto const result = prometheus_server_->start(); !result) {
            io::throw_boot_failure(result, "prometheus server start");
        }
    }
    load_policy("boot");
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

void Controller::Impl::on_expired(io::TimerToken /*id*/) {
    // 이 핸들러로 오는 타이머는 주기 sweep 뿐이다. 한 tick의 now를 모든 호출에 공유한다.
    auto const now = clock_.now();
    // sweep 도중 예외가 나가도 다음 tick은 예약한다. 마지막 줄에만 두면 한 번의 실패로
    // 재전송/축출/정책 평가가 영구히 멈춘다.
    // 소멸자에 두지 않는 이유는 소멸자가 noexcept라, 재무장 자체가 실패하면 예외가 나갈 곳이
    // 없어 terminate가 되기 때문이다. 여기서 던지면 main의 catch가 한 줄로 알리고 끝낸다.
    try {
        command_service_.sweep(now);
        session_service_.sweep(now);   // 등록 시한 초과 disconnect + active 침묵 evict
        policy_service_.evaluate(now); // 그룹 load 집계 후 임계 전환 시 SetMode 발신
        sweep_stats_.record(clock_.now() - now); // tick 작업 소요(schedule 제외) 기록
    } catch (...) {
        schedule_sweep();
        throw;
    }
    schedule_sweep();
}

void Controller::Impl::schedule_sweep() {
    sweep_timer_ = timer_scheduler_.schedule(cfg_.sweep_interval, *this);
}

void Controller::Impl::load_policy(std::string_view trigger) {
    if (!cfg_.policy_path) {
        return; // 정책 비활성 (빈 정책이면 evaluate no-op)
    }
    auto const& path = *cfg_.policy_path;
    std::ifstream file{path};
    if (!file) {
        LOG_POLICY_LOAD_FAIL(path.string(), "open", trigger);
        return;
    }
    std::string const text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    auto const json = json::parse(text);
    if (!json) {
        LOG_POLICY_LOAD_FAIL(path.string(), "parse", trigger);
        return;
    }
    // 정책은 controller 설정 파일에 인라인된 "policy" 객체다.
    auto const* policy_node = json->find("policy");
    if (policy_node == nullptr) {
        LOG_POLICY_LOAD_ABSENT(path.string(), trigger);
        return; // 부팅이면 빈 정책(evaluate no-op), 리로드면 옛 정책이 그대로 남는다
    }
    auto policy = app::device::parse_policy(*policy_node);
    if (!policy) {
        LOG_POLICY_LOAD_FAIL(path.string(), "invalid", trigger);
        return;
    }
    LOG_POLICY_LOAD(path.string(), policy->size(), trigger);
    policy_service_.set_policy(std::move(*policy));
}

void Controller::Impl::handle_signal(int sig) {
    if (sig == SIGHUP) {
        load_policy("reload"); // 재적재 자체는 policy.load* 의 trigger 가 말한다
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

std::uint16_t Controller::prometheus_port() const {
    return impl_->prometheus_port();
}

} // namespace ddcs::ctrl
