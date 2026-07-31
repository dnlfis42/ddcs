#include "ddcs/config/env.hpp"
#include "ddcs/config/file.hpp"
#include "ddcs/ctrl/controller.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <utility>

int main() {
    ddcs::logger::StdoutSink sink;
    auto& lg = ddcs::logger::Logger::instance();
    lg.set_sink(sink);

    // 부팅 실패(config malformed, epoll/signalfd/timerfd 생성 실패 등)는 여기서 잡아 운영자용
    // 1줄 진단 + EXIT_FAILURE로 끝낸다.
    try {
        // 설정 파일
        std::filesystem::path const config_path{
            ddcs::config::env::get("DDCS_CONFIG_PATH").value_or("config/controller.json")
        };
        LOG_CONFIG_PATH("DDCS_CONFIG_PATH", config_path.string());

        ddcs::json::Value root; // 파일 없으면 null -> 전부 기본값
        if (auto loaded = ddcs::config::file::load(config_path)) {
            root = std::move(*loaded);
        } else {
            LOG_CONFIG_PATH_ABSENT(config_path.string());
        }

        // 로거 설정
        auto log_level = lg.level();
        auto const level_text = ddcs::config::file::get_string(root, "log.level", "info");
        if (auto const parsed = ddcs::logger::parse_level(level_text)) {
            log_level = *parsed;
        } else {
            LOG_CONFIG_VALUE_INVALID("file", "log.level", "log level", level_text);
        }
        if (auto const level = ddcs::config::env::get("DDCS_LOG_LEVEL")) {
            if (auto const parsed = ddcs::logger::parse_level(*level)) {
                log_level = *parsed;
            } else {
                LOG_CONFIG_VALUE_INVALID("env", "DDCS_LOG_LEVEL", "log level", *level);
            }
        }
        lg.set_level(log_level);

        // Controller 설정. 절 순서는 controller.json을 따른다.
        ddcs::ctrl::Controller::Config cfg{};

        // controller
        cfg.sweep_interval = ddcs::config::file::get_duration_ms(
            root, "controller.sweep_interval_ms", cfg.sweep_interval
        );

        // prometheus
        cfg.prometheus_port = ddcs::config::file::get_port(root, "prometheus.port", 9000);
        if (auto const port = ddcs::config::env::get_port("DDCS_PROMETHEUS_PORT")) {
            cfg.prometheus_port = *port;
        }

        // transport
        cfg.listen_port = ddcs::config::file::get_port(root, "transport.port", 8080);
        cfg.listen_port = ddcs::config::env::get_port("DDCS_TRANSPORT_PORT", cfg.listen_port);

        cfg.accept_backlog =
            ddcs::config::file::get_int(root, "transport.accept_backlog", cfg.accept_backlog);

        cfg.rx_buffer_size = ddcs::config::file::get_size(
            root, "transport.rx_buffer_size", cfg.rx_buffer_size, ddcs::wire::frame::max_rx_capacity
        );

        // session
        cfg.handshake_timeout = ddcs::config::file::get_duration_ms(
            root, "session.handshake_timeout_ms", cfg.handshake_timeout
        );
        cfg.liveness_timeout = ddcs::config::file::get_duration_ms(
            root, "session.liveness_timeout_ms", cfg.liveness_timeout
        );

        // command
        cfg.command_timeout =
            ddcs::config::file::get_duration_ms(root, "command.timeout_ms", cfg.command_timeout);
        cfg.command_max_attempts =
            ddcs::config::file::get_int(root, "command.max_attempts", cfg.command_max_attempts);
        cfg.command_backoff_base = ddcs::config::file::get_duration_ms(
            root, "command.backoff_base_ms", cfg.command_backoff_base
        );

        // policy
        cfg.policy_path = config_path; // 정책은 같은 파일의 "policy" 객체에 인라인

        // 컨트롤러 조립
        ddcs::ctrl::Controller controller{std::move(cfg)};
        controller.start();
        controller.run();
    } catch (std::exception const& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        std::fprintf(stderr, "unknown exception\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
