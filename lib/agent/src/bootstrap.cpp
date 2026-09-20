#include "ddcs/agent/bootstrap.hpp"
#include "ddcs/agent/domain/simulated_device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/config/env.hpp"
#include "ddcs/config/file.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <utility>

namespace ddcs::agent::bootstrap {

Agent::Config load_agent_config() {
    auto& lg = logger::Logger::instance();

    std::filesystem::path const config_path{
        ddcs::config::env::get("DDCS_CONFIG_PATH").value_or("config/agent.json")
    };
    LOG_CONFIG_PATH("DDCS_CONFIG_PATH", config_path.string());

    ddcs::json::Value root; // 파일이 없으면 기본값과 환경변수 설정을 사용한다.
    if (auto loaded = ddcs::config::file::load(config_path)) {
        root = std::move(*loaded);
    } else {
        LOG_CONFIG_PATH_ABSENT(config_path.string());
    }

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

    ddcs::agent::Agent::Config cfg{};

    cfg.controller_host =
        ddcs::config::file::get_string(root, "transport.host", cfg.controller_host);
    if (auto const host = ddcs::config::env::get("DDCS_TRANSPORT_HOST")) {
        cfg.controller_host = std::string{*host};
    }

    cfg.controller_port = ddcs::config::file::get_port(root, "transport.port", cfg.controller_port);
    cfg.controller_port = ddcs::config::env::get_port("DDCS_TRANSPORT_PORT", cfg.controller_port);

    cfg.rx_buffer_size = ddcs::config::file::get_size(
        root, "transport.rx_buffer_size", cfg.rx_buffer_size, ddcs::wire::frame::max_rx_capacity
    );

    cfg.reconnect_base_delay = ddcs::config::file::get_duration_ms(
        root, "transport.reconnect_base_delay_ms", cfg.reconnect_base_delay
    );

    cfg.reconnect_max_delay = ddcs::config::file::get_duration_ms(
        root, "transport.reconnect_max_delay_ms", cfg.reconnect_max_delay
    );

    cfg.session.heartbeat = ddcs::config::file::get_duration_ms(
        root, "session.heartbeat_interval_ms", cfg.session.heartbeat
    );

    cfg.session.status_report = ddcs::config::file::get_duration_ms(
        root, "session.status_report_interval_ms", cfg.session.status_report
    );

    cfg.session.register_timeout = ddcs::config::file::get_duration_ms(
        root, "session.registration_timeout_ms", cfg.session.register_timeout
    );

    cfg.session.group = ddcs::config::file::get_string(root, "device.group", "zone_a");
    if (auto const group = ddcs::config::env::get("DDCS_DEVICE_GROUP")) {
        cfg.session.group = std::string{*group};
    }

    return cfg;
}

std::unique_ptr<domain::Device>
make_simulated_device(common::Uuid uuid, Agent::Config const& cfg, std::mt19937_64& rng) {
    // 상태 보고 때마다 장치를 갱신하므로 보고 주기를 적분 간격으로 사용한다.
    ddcs::agent::domain::SimulatedDevice::Config sim{};
    sim.tick_seconds =
        static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(cfg.session.status_report).count()
        ) /
        1000.0;
    sim.load_noise = ddcs::config::env::get_double("DDCS_SIM_NOISE", sim.load_noise);
    sim.temp_noise = sim.load_noise * 0.5;

    // 부하 변화율에만 개체차를 주고 발열·냉각 속도는 공통으로 유지한다.
    // 변화율의 부호를 보존하도록 jitter를 1 미만으로 제한한다. 0 이하는 개체차를 끈다.
    double jitter = ddcs::config::env::get_double("DDCS_SIM_JITTER", 0.10);
    if (jitter > 0.99) {
        jitter = 0.99;
    }
    auto vary = [&](double v) {
        if (jitter <= 0.0) {
            return v;
        }
        std::uniform_real_distribution<double> d{1.0 - jitter, 1.0 + jitter};
        return v * d(rng);
    };
    sim.load_rate_performance = vary(sim.load_rate_performance);
    sim.load_rate_normal = vary(sim.load_rate_normal);
    sim.load_rate_safe = vary(sim.load_rate_safe);
    sim.load_initial = std::uniform_real_distribution<double>{20.0, 80.0}(rng);
    sim.temp_initial = std::uniform_real_distribution<double>{40.0, 55.0}(rng);
    sim.seed = rng();
    auto device = std::make_unique<ddcs::agent::domain::SimulatedDevice>(
        uuid, ddcs::device::Mode::normal, sim
    );

    return device;
}

} // namespace ddcs::agent::bootstrap
