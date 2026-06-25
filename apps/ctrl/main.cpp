#include "ddcs/config/config.hpp"
#include "ddcs/ctrl/controller.hpp"
#include "ddcs/logger/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>

namespace {

std::string env_or(char const* name, char const* fallback) {
    char const* const v = std::getenv(name);
    return v != nullptr ? std::string{v} : std::string{fallback};
}

// 없으면 기본값으로 동작하되 운영자에게 알린다(에러 아님).
void load_or_note(ddcs::config::Config& conf, std::filesystem::path const& path) {
    if (!conf.add_file(path)) {
        std::fprintf(stderr, "ddcs-ctrl: config %s not found; using defaults\n", path.c_str());
    }
}

} // namespace

int main() {
    // 부팅 실패(config malformed, 포트 점유 EADDRINUSE, epoll/timerfd 생성 실패 등)는 여기서 잡아
    // 운영자용 1줄 진단 + EXIT_FAILURE로 끝낸다. (config malformed는 add_file이 throw)
    try {
        std::filesystem::path const dir = env_or("DDCS_CONFIG_DIR", "config");
        ddcs::config::Config conf;
        load_or_note(conf, dir / "network.json");    // 공유 엔드포인트
        load_or_note(conf, dir / "controller.json"); // controller 전용

        ddcs::ctrl::Controller::Config cfg{};
        // 우선순위: env > 파일 > 코드 기본값
        cfg.listen_port = conf.get_port("controller.port", "DDCS_CONTROLLER_PORT", 8080);
        cfg.accept_backlog = conf.get_int("accept_backlog", nullptr, 128);
        cfg.metrics_port = conf.get_port("metrics_port", "DDCS_METRICS_PORT", 9090);
        cfg.handshake_timeout = conf.get_duration_ms("timeout_ms.handshake", 3000);
        cfg.liveness_timeout = conf.get_duration_ms("timeout_ms.liveness", 3000);
        cfg.command_timeout = conf.get_duration_ms("timeout_ms.command", 5000);
        cfg.command_max_attempts = conf.get_int("command.max_attempts", nullptr, 3);
        cfg.command_backoff_base = conf.get_duration_ms("command.backoff_base_ms", 500);
        cfg.sweep_interval = conf.get_duration_ms("sweep_interval_ms", 1000);
        cfg.policy_path = std::filesystem::path{
            conf.get_string("policy_path", "DDCS_POLICY_PATH", "config/policy.json")
        };
        if (auto const level =
                ddcs::logger::parse_level(conf.get_string("log_level", "DDCS_LOG_LEVEL", "info"))) {
            cfg.log_level = *level;
        }

        ddcs::ctrl::Controller controller{std::move(cfg)};
        controller.start();
        controller.run();
    } catch (std::exception const& e) {
        std::fprintf(stderr, "ddcs-ctrl: fatal: %s\n", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        std::fprintf(stderr, "ddcs-ctrl: fatal: unknown error\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
