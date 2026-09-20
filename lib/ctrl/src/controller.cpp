#include "ddcs/ctrl/controller.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/device/command_service.hpp"
#include "ddcs/ctrl/app/device/policy_service.hpp"
#include "ddcs/ctrl/app/device/registration_service.hpp"
#include "ddcs/ctrl/app/device/status_service.hpp"
#include "ddcs/ctrl/app/metrics/metrics_service.hpp"
#include "ddcs/ctrl/app/metrics/tick_stats.hpp"
#include "ddcs/ctrl/app/session/command_sender.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/app/session/session_service.hpp"
#include "ddcs/ctrl/detail/fixed_rate_schedule.hpp"
#include "ddcs/ctrl/detail/tick_execution.hpp"
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
#include "ddcs/profile/recorder.hpp"
#include "ddcs/profile/timestamp_converter.hpp"

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

} // namespace

class Controller::Impl final : public io::TimerHandler {
public:
    explicit Impl(Config cfg, profile::Recorder* profile_recorder);
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
    [[nodiscard]] std::optional<std::uint64_t> relative_profile_ns(common::Clock::time_point time
    ) const noexcept {
        if (!profile_timestamp_converter_) {
            return std::nullopt;
        }

        return profile_timestamp_converter_->relative_ns(time);
    }
    // 시작 시와 SIGHUP 수신 시 정책을 읽는다. 실패하면 경고를 기록하고 기존 정책을 유지한다.
    // trigger는 "boot" 또는 "reload"이며 정책 로딩 로그에 포함된다.
    void load_policy(std::string_view trigger);
    void handle_signal(int sig); // SIGHUP은 정책을 다시 읽고, SIGINT와 SIGTERM은 실행을 중지한다.

    common::SteadyClock clock_;
    profile::Recorder* const profile_recorder_;
    std::optional<profile::TimestampConverter> profile_timestamp_converter_;
    std::uint64_t next_profile_tick_id_ = 1;
    Config cfg_;

    io::Reactor reactor_;
    io::SignalSource signal_source_;
    io::TimerScheduler timer_scheduler_;

    // sender()와 disconnector()를 사용하는 객체보다 먼저 생성한다.
    infra::transport::Server transport_server_;

    app::session::SessionRegistry session_registry_;
    domain::DeviceRegistry device_registry_;

    app::session::CommandSender command_sender_;
    app::device::CommandService command_service_;

    app::device::RegistrationService registration_service_;
    app::device::StatusService status_service_;
    app::device::PolicyService policy_service_;
    // 연결 및 메시지 이벤트를 처리하고 세션의 등록·응답 기한을 점검한다.
    app::session::SessionService session_service_;
    app::metrics::TickStats sweep_stats_; // 참조하는 metrics_service_보다 먼저 생성하고 나중에 소멸한다.
    app::metrics::MetricsService metrics_service_;

    // 메트릭 포트가 설정되어 있으면 start()에서 서버를 생성한다.
    // metrics_service_보다 먼저 소멸시켜 메트릭 참조를 유효하게 유지한다.
    std::optional<infra::prometheus::Server> prometheus_server_;

    detail::FixedRateSchedule sweep_schedule_;
    io::TimerToken sweep_timer_;
};

Controller::Impl::Impl(Config cfg, profile::Recorder* profile_recorder)
    : profile_recorder_(profile_recorder),
      cfg_(std::move(cfg)),
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
          session_service_, policy_service_.policy(), sweep_stats_, transport_server_.stats_source()
      ),
      sweep_schedule_(cfg_.sweep_interval) {}

Controller::Impl::~Impl() {
    stop();
    // 서버를 닫으면 SessionService에 연결 종료를 알린다.
    // 멤버 소멸 순서상 서버가 더 늦게 소멸하므로, SessionService가 유효할 때 닫는다.
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
        // 메트릭 요청을 위한 연결 대기열 크기
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
    if (profile_recorder_ != nullptr && !profile_timestamp_converter_) {
        // 첫 tick을 예약하기 직전에 프로파일 시각의 기준점을 설정한다.
        profile_timestamp_converter_.emplace(clock_.now());
    }
    sweep_schedule_.start(clock_.now());
    sweep_timer_ = timer_scheduler_.schedule_at(sweep_schedule_.deadline(), *this);
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
    // 주기 타이머를 처리한다. 명령·세션·정책 처리에 동일한 tick 시작 시각을 전달한다.
    auto const now = clock_.now();
    sweep_stats_.start_lateness.record(sweep_schedule_.lateness(now));
    std::optional<profile::TickSample> profile_sample;
    if (profile_recorder_ != nullptr && profile_timestamp_converter_) {
        auto const tick_id = next_profile_tick_id_++;
        if (auto const started_ns = relative_profile_ns(now)) {
            profile_sample.emplace(profile::TickSample{
                .tick_id = tick_id,
                .started_ns = *started_ns,
                .command_sweep_ended_ns = 0,
                .session_sweep_ended_ns = 0,
                .policy_evaluate_ended_ns = 0,
                .finished_ns = 0,
                .outcome = profile::TickOutcome::completed,
            });
        }
    }

    // tick 처리에 실패해도 다음 실행을 예약한다.
    // 예약 중 발생한 예외는 호출자에게 전달할 수 있도록 소멸자 밖에서 처리한다.
    auto const failure = detail::execute_tick_phases(
        [this, now, &profile_sample] {
            command_service_.sweep(now);
            if (profile_sample) {
                if (auto const ended_ns = relative_profile_ns(clock_.now())) {
                    profile_sample->command_sweep_ended_ns = *ended_ns;
                } else {
                    profile_sample.reset();
                }
            }
        },
        [this, now, &profile_sample] {
            session_service_.sweep(now); // 등록 또는 응답 기한이 지난 세션을 정리한다.
            if (profile_sample) {
                if (auto const ended_ns = relative_profile_ns(clock_.now())) {
                    profile_sample->session_sweep_ended_ns = *ended_ns;
                } else {
                    profile_sample.reset();
                }
            }
        },
        [this, now, &profile_sample] {
            policy_service_.evaluate(now); // Group 부하와 Device 상태에 따라 모드 변경을 요청한다.
            auto const finished = clock_.now();
            sweep_stats_.work.record(finished - now); // 다음 예약에 걸리는 시간을 제외한 tick 작업 시간을 기록한다.
            if (profile_sample) {
                if (auto const finished_ns = relative_profile_ns(finished)) {
                    profile_sample->policy_evaluate_ended_ns = *finished_ns;
                    profile_sample->finished_ns = *finished_ns;
                    profile_sample->outcome = profile::TickOutcome::completed;
                    profile_recorder_->record(*profile_sample);
                }
            }
        }
    );
    if (failure) {
        if (profile_sample) {
            if (auto const finished_ns = relative_profile_ns(clock_.now())) {
                profile_sample->finished_ns = *finished_ns;
                profile_sample->outcome = failure->outcome;
                profile_recorder_->record(*profile_sample);
            }
        }
        schedule_sweep();
        std::rethrow_exception(failure->exception);
    }
    schedule_sweep();
}

void Controller::Impl::schedule_sweep() {
    sweep_stats_.skipped_total += sweep_schedule_.advance(clock_.now());
    sweep_timer_ = timer_scheduler_.schedule_at(sweep_schedule_.deadline(), *this);
}

void Controller::Impl::load_policy(std::string_view trigger) {
    if (!cfg_.policy_path) {
        return; // 정책이 설정되지 않았으면 평가하지 않는다.
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
    // Controller 설정 파일의 "policy" 객체를 읽는다.
    auto const* policy_node = json->find("policy");
    if (policy_node == nullptr) {
        LOG_POLICY_LOAD_ABSENT(path.string(), trigger);
        return; // 시작 시에는 빈 정책을, 다시 읽을 때는 기존 정책을 유지한다.
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
        load_policy("reload"); // 정책을 다시 읽은 결과는 trigger="reload"로 기록한다.
        return;
    }
    stop(); // SIGINT / SIGTERM
}

Controller::Controller(Config cfg, profile::Recorder* profile_recorder)
    : impl_(std::make_unique<Impl>(std::move(cfg), profile_recorder)) {}

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
