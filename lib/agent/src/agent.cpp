#include "ddcs/agent/agent.hpp"

#include "ddcs/io/throw_errno.hpp"

#include "ddcs/agent/infra/transport/backoff_schedule.hpp"
#include "ddcs/agent/infra/transport/connector.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/io/signal_source.hpp"
#include "ddcs/io/timer_scheduler.hpp"

#include <csignal>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>

namespace ddcs::agent {

class Agent::Impl final {
public:
    Impl(Config cfg, std::unique_ptr<domain::Device> device);
    ~Impl();

    Impl(Impl const&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    void start();
    void run();
    void run_once(std::chrono::milliseconds timeout);
    void stop();

    app::session::SessionService& session() noexcept {
        return session_;
    }

private:
    std::unique_ptr<domain::Device> device_;

    io::Reactor reactor_;

    io::SignalSource signal_source_;
    io::TimerScheduler timer_scheduler_;

    infra::transport::Connector connector_;
    app::session::SessionService session_;
};

namespace {

// device는 필수 의존이라 부재는 프로그래머 오류다. 멤버 초기화 목록의 역참조(*device_)보다
// 먼저 검증해 UB 대신 진단 가능한 실패로 만든다.
std::unique_ptr<domain::Device> require_device(std::unique_ptr<domain::Device> device) {
    if (device == nullptr) {
        throw std::invalid_argument{"agent: device is required"};
    }
    return device;
}

} // namespace

Agent::Impl::Impl(Config cfg, std::unique_ptr<domain::Device> device)
    : device_(require_device(std::move(device))),
      signal_source_(reactor_, {SIGINT, SIGTERM}, [this](int) { stop(); }),
      timer_scheduler_(reactor_),
      connector_(
          reactor_, timer_scheduler_, cfg.controller_host, cfg.controller_port, cfg.rx_buffer_size,
          // seed는 설정이 아니라 엔트로피다. 조립 루트가 부팅 시 1회 뽑아 jitter 수열을
          // 프로세스마다 가른다(테스트는 고정 seed로 결정성 유지).
          infra::transport::BackoffSchedule{
              cfg.reconnect_base_delay, cfg.reconnect_max_delay, std::random_device{}()
          }
      ),
      session_(*device_, connector_, cfg.session) {
    connector_.init(session_); // inbound 포트 주입
}

Agent::Impl::~Impl() {
    stop();
    // 멤버 dtor 역순:
    // session_, connector_, timer_scheduler_, signal_source_, reactor_, device_
}

void Agent::Impl::start() {
    signal_source_.start();
    timer_scheduler_.start();
    if (auto const result = connector_.start(); !result) {
        io::throw_boot_failure(result, "agent transport start");
    }
}

void Agent::Impl::run() {
    reactor_.run();
}

void Agent::Impl::run_once(std::chrono::milliseconds timeout) {
    reactor_.run_once(timeout);
}

void Agent::Impl::stop() {
    timer_scheduler_.stop();
    signal_source_.stop();
    reactor_.stop();
}

Agent::Agent(Config cfg, std::unique_ptr<domain::Device> device)
    : impl_{std::make_unique<Impl>(std::move(cfg), std::move(device))} {}

Agent::~Agent() = default;

void Agent::start() {
    impl_->start();
}

void Agent::run() {
    impl_->run();
}

void Agent::run_once(std::chrono::milliseconds timeout) {
    impl_->run_once(timeout);
}

void Agent::stop() {
    impl_->stop();
}

app::session::SessionService& Agent::session() noexcept {
    return impl_->session();
}

} // namespace ddcs::agent
