#include "ddcs/ctrl/controller.hpp"

#include "ddcs/json/value.hpp"

#include <fstream>
#include <iterator>
#include <string>

namespace ddcs::ctrl {

Controller::Controller(Config cfg)
    : cfg_{cfg}, coordinator_{reactor_}, acceptor_{reactor_, coordinator_, cfg.listen_port, cfg.accept_backlog},
      registrar_{sessions_, registry_, coordinator_, clock_}, status_{sessions_, registry_},
      commands_{
          sessions_, coordinator_, clock_, cfg.command_timeout, cfg.command_max_attempts, cfg.command_backoff_base
      },
      session_manager_{sessions_, registrar_, status_, commands_, coordinator_, clock_},
      operator_service_{registry_, commands_}, policy_{sessions_, registry_, operator_service_},
      liveness_{sessions_, coordinator_, clock_, cfg.liveness_timeout},
      metrics_service_{sessions_, registry_, commands_, registrar_, liveness_} {
    auto& lg = logger::Logger::instance();
    lg.set_level(cfg.log_level);
    lg.set_sink(cfg.log_sink != nullptr ? *cfg.log_sink : default_sink_);

    coordinator_.init(session_manager_); // inbound 포트 주입
}

Controller::~Controller() {
    stop();
    // 멤버 dtor 역순: session_manager_ -> ... -> coordinator_ -> reactor_ (reactor 가 마지막에 소멸).
}

void Controller::start() {
    acceptor_.start();
    if (cfg_.metrics_port) {
        constexpr int metrics_backlog{16}; // 스크레이프는 저빈도 - 작은 backlog 로 충분
        metrics_server_.emplace(reactor_, metrics_service_, *cfg_.metrics_port, metrics_backlog);
        metrics_server_->start();
    }
    load_policy();
    schedule_sweep();
}

void Controller::run() { reactor_.run(); }

void Controller::run_once(std::chrono::milliseconds timeout) { reactor_.run_once(timeout); }

void Controller::stop() { reactor_.stop(); }

void Controller::on_timer(io::TimerId /*id*/) {
    // 이 핸들러로 오는 타이머는 주기 sweep 뿐이다.
    commands_.sweep();
    liveness_.sweep();                // active 세션 침묵 -> evict(close)
    policy_.evaluate();               // 그룹 load 집계 -> 임계 전환 시 SetMode 발신
    coordinator_.close_connections(); // sweep/evaluate 의 close 는 entry-point 밖 -> 여기서 reap 구동
    schedule_sweep();                 // 주기 재무장
}

void Controller::schedule_sweep() { sweep_timer_ = reactor_.schedule(cfg_.sweep_interval, this); }

void Controller::load_policy() {
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

} // namespace ddcs::ctrl
