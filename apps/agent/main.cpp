#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/domain/simulated_device.hpp"
#include "ddcs/common/parse.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/config/env.hpp"
#include "ddcs/config/file.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

std::string_view trim(std::string_view s) noexcept {
    auto const first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// 인스턴스별 device 신원. env(DDCS_DEVICE_ID) > 파일 읽기 > 생성 순으로 해석한다.
// 파일 경로를 지정하지 않으면 기록하지 않고 기동할 때마다 새로 발급한다.
ddcs::common::Uuid load_device_uuid() {
    if (auto const value = ddcs::config::env::get("DDCS_DEVICE_ID")) {
        if (auto u = ddcs::common::parse_uuid(*value)) {
            LOG_DEVICE_ID(u->to_string(), "env");
            return *u;
        }
        LOG_CONFIG_VALUE_INVALID("env", "DDCS_DEVICE_ID", "uuid", *value);
    }

    auto const configured = ddcs::config::env::get("DDCS_DEVICE_ID_FILE");
    if (!configured) {
        // 신원을 남기지 않는 쪽을 고른 것이다. 한 디렉터리에서 여러 대를 띄워도
        // 같은 DeviceId로 등록해 서로를 kick-old로 끊어내는 일이 없다.
        ddcs::common::Uuid const fresh = ddcs::common::Uuid::random();
        LOG_DEVICE_ID(fresh.to_string(), "ephemeral");
        return fresh;
    }

    std::filesystem::path const path{*configured};
    LOG_CONFIG_PATH("DDCS_DEVICE_ID_FILE", path.string());

    if (std::ifstream in{path}) {
        std::string const text{
            std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}
        };
        auto const trimmed = trim(text);
        if (auto u = ddcs::common::parse_uuid(trimmed)) {
            LOG_DEVICE_ID(u->to_string(), "file");
            return *u;
        }
        // 파일 하나가 통째로 값이라 key 자리에 경로가 온다.
        LOG_CONFIG_VALUE_INVALID("file", path.string(), "uuid", trimmed);
    }

    ddcs::common::Uuid const fresh = ddcs::common::Uuid::random();
    LOG_DEVICE_ID(fresh.to_string(), "generated");
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (std::ofstream out{path}) {
        out << fresh.to_string() << '\n';
        if (out) {
            return fresh; // persist 성공 -> 안정 신원
        }
    }

    // 기록에 실패하면 재시작마다 DeviceId가 바뀌어 Controller가 매번 새 Device로 본다.
    LOG_DEVICE_ID_NOT_PERSISTED(fresh.to_string());
    return fresh;
}

} // namespace

int main() {
    ddcs::logger::StdoutSink sink;
    auto& lg = ddcs::logger::Logger::instance();
    lg.set_sink(sink);

    // 부팅 실패(config malformed, epoll/signalfd/timerfd 생성 실패 등)는 여기서 잡아 운영자용
    // 1줄 진단 + EXIT_FAILURE로 끝낸다.
    try {
        // device uuid
        auto const uuid = load_device_uuid();

        // 설정 파일
        std::filesystem::path const config_path{
            ddcs::config::env::get("DDCS_CONFIG_PATH").value_or("config/agent.json")
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

        // Agent 설정
        ddcs::agent::Agent::Config cfg{};

        cfg.controller_host =
            ddcs::config::file::get_string(root, "transport.host", cfg.controller_host);
        if (auto const host = ddcs::config::env::get("DDCS_TRANSPORT_HOST")) {
            cfg.controller_host = std::string{*host};
        }

        cfg.controller_port =
            ddcs::config::file::get_port(root, "transport.port", cfg.controller_port);
        cfg.controller_port =
            ddcs::config::env::get_port("DDCS_TRANSPORT_PORT", cfg.controller_port);

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

        // device 시뮬레이션:
        // load/temp는 mode-구동 상태(정책이 SetMode로 몰고 sim이 rate*tick 적분).
        // rate(초당)는 device 스펙(글로벌 기본값), tick은 status 보고 주기에서 주입.
        // sim 노브(DDCS_SIM_*)는 데모 조정용 env 전용 키다.
        ddcs::agent::domain::SimulatedDevice::Config sim{};
        sim.tick_seconds =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::milliseconds>(cfg.session.status_report)
                    .count()
            ) /
            1000.0;
        sim.load_noise = ddcs::config::env::get_double("DDCS_SIM_NOISE", sim.load_noise);
        sim.temp_noise = sim.load_noise * 0.5;
        // 개체차: load rate는 +-jitter(평균 보존), 초기 load/temp는 랜덤, noise seed도 device마다.
        // temp rate(발열/냉각)는 기기 공통 스펙이라 안 흔든다. 개체 분산은 초기온도+noise 몫이다.
        std::mt19937_64 rng{std::random_device{}()};
        // jitter는 부호 보존 위해 (0,1): 1 이상이면 load_rate가 0/부호반전돼 limit cycle이 깨진다.
        // (음수/0은 아래 lambda가 그대로 통과시켜 무효화한다.)
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

        ddcs::agent::Agent agent{std::move(cfg), std::move(device)};
        agent.start();
        agent.run();
    } catch (std::exception const& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        std::fprintf(stderr, "unknown exception\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
