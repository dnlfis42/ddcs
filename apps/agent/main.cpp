#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/domain/simulated_device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/config/config.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"

#include <array>
#include <chrono>
#include <cstdint>
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

namespace {

std::string env_or(char const* name, char const* fallback) {
    char const* const v = std::getenv(name);
    return v != nullptr ? std::string{v} : std::string{fallback};
}

// SimulatedDevice 파라미터 override용. 없거나 파싱 실패 시 fallback.
double env_double(char const* name, double fallback) {
    char const* const v = std::getenv(name);
    if (v == nullptr) {
        return fallback;
    }
    char* end = nullptr;
    double const parsed = std::strtod(v, &end);
    return end != v ? parsed : fallback;
}

void load_or_note(ddcs::config::Config& conf, std::filesystem::path const& path) {
    if (!conf.add_file(path)) {
        std::fprintf(stderr, "ddcs-agent: config %s not found; using defaults\n", path.c_str());
    }
}

constexpr int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// 32 char hex (no dashes). 예: "0feef128d17f1f5585659a23ddb8c29d"
std::optional<ddcs::common::Uuid> parse_uuid_hex32(std::string_view s) noexcept {
    if (s.size() != 32) {
        return std::nullopt;
    }
    std::array<std::byte, 16> bytes{};
    for (std::size_t i = 0; i < 16; ++i) {
        int const hi = hex_value(s[i * 2]);
        int const lo = hex_value(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        bytes[i] = std::byte{static_cast<std::uint8_t>(hi * 16 + lo)};
    }
    return ddcs::common::Uuid{bytes};
}

// 표준 8-4-4-4-12 형식 (uuidgen / Uuid::to_string 호환). 예: "0feef128-d17f-1f55-8565-9a23ddb8c29d"
std::optional<ddcs::common::Uuid> parse_uuid_hex36(std::string_view s) noexcept {
    if (s.size() != 36) {
        return std::nullopt;
    }
    if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
        return std::nullopt;
    }
    std::array<std::byte, 16> bytes{};
    std::size_t out_nibble = 0;
    for (char const c : s) {
        if (c == '-') {
            continue;
        }
        int const h = hex_value(c);
        if (h < 0) {
            return std::nullopt;
        }
        if (out_nibble % 2 == 0) {
            bytes[out_nibble / 2] = std::byte{static_cast<std::uint8_t>(h << 4)};
        } else {
            auto const merged = static_cast<std::uint8_t>(
                static_cast<std::uint8_t>(bytes[out_nibble / 2]) | static_cast<std::uint8_t>(h)
            );
            bytes[out_nibble / 2] = std::byte{merged};
        }
        ++out_nibble;
    }
    return ddcs::common::Uuid{bytes};
}

// 두 형식 중 하나로 파싱하고 nil(전부 0)은 신원으로 무의미하므로 거부한다.
std::optional<ddcs::common::Uuid> parse_device_uuid(std::string_view s) noexcept {
    std::optional<ddcs::common::Uuid> u = parse_uuid_hex36(s);
    if (!u.has_value()) {
        u = parse_uuid_hex32(s);
    }
    if (u.has_value() && u->is_nil()) {
        return std::nullopt;
    }
    return u;
}

std::string_view trim(std::string_view s) noexcept {
    auto const first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

ddcs::common::Uuid generate_random_uuid() {
    std::random_device rd;
    std::mt19937_64 gen{rd()};
    std::array<std::byte, 16> bytes{};
    for (auto& b : bytes) {
        b = std::byte{static_cast<std::uint8_t>(gen() & 0xff)};
    }
    return ddcs::common::Uuid{bytes};
}

struct LoadedUuid {
    ddcs::common::Uuid value;
    bool ephemeral; // 안정 신원이 아니라 매번 달라질 수 있음(persist 실패)
};

// 인스턴스별 device 신원. 우선순위:
// 1. DDCS_DEVICE_ID (env, 명시)
// 2. DDCS_DEVICE_ID_FILE (기본 data/agent.uuid) 읽기 - 재시작에도 안정
// 3. 랜덤 생성 후 그 파일에 기록 - 첫 부팅 자가발급(이후 안정). 기록 실패 시에만 ephemeral.
LoadedUuid load_device_uuid() {
    if (char const* env = std::getenv("DDCS_DEVICE_ID")) {
        if (auto u = parse_device_uuid(env)) {
            return {*u, false};
        }
        std::fprintf(stderr, "ddcs-agent: DDCS_DEVICE_ID is invalid or nil; ignoring\n");
    }

    std::filesystem::path const path = env_or("DDCS_DEVICE_ID_FILE", "data/agent.uuid");

    if (std::ifstream in{path}) {
        std::string const text{
            std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}
        };
        if (auto u = parse_device_uuid(trim(text))) {
            return {*u, false};
        }
        std::fprintf(stderr, "ddcs-agent: %s holds an invalid uuid; regenerating\n", path.c_str());
    }

    ddcs::common::Uuid const fresh = generate_random_uuid();
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (std::ofstream out{path}) {
        out << fresh.to_string() << '\n';
        if (out) {
            return {fresh, false}; // persist 성공 -> 안정 신원
        }
    }
    std::fprintf(
        stderr, "ddcs-agent: could not persist uuid to %s; using a random ephemeral id\n",
        path.c_str()
    );
    return {fresh, true};
}

} // namespace

int main() {
    // 부팅 실패(config malformed, epoll/signalfd/timerfd 생성 실패 등)는 여기서 잡아 운영자용
    // 1줄 진단 + EXIT_FAILURE로 끝낸다. (config malformed는 add_file이 throw)
    try {
        std::filesystem::path const path = env_or("DDCS_CONFIG_PATH", "config/agent.json");
        ddcs::config::Config conf;
        load_or_note(conf, path); // 단일 agent 설정 파일 (신원은 별도 env/파일로 해석)

        auto const uuid = load_device_uuid();

        ddcs::agent::Agent::Config cfg{};
        cfg.controller_host = conf.get_string("transport.host", "DDCS_TRANSPORT_HOST", "127.0.0.1");
        cfg.controller_port = conf.get_port("transport.port", "DDCS_TRANSPORT_PORT", 8080);
        cfg.reconnect_base_delay = conf.get_duration_ms("transport.reconnect_base_delay_ms", 1000);
        cfg.reconnect_max_delay = conf.get_duration_ms("transport.reconnect_max_delay_ms", 30000);
        cfg.device_id_is_ephemeral = uuid.ephemeral;
        cfg.session.group = conf.get_string("device.group", "DDCS_DEVICE_GROUP", "zone_a");
        cfg.session.heartbeat = conf.get_duration_ms("session.heartbeat_interval_ms", 1000);
        cfg.session.status_update = conf.get_duration_ms("session.status_report_interval_ms", 5000);
        cfg.session.register_timeout =
            conf.get_duration_ms("session.registration_timeout_ms", 2000);
        // device 시뮬레이션: load/temp는 mode-구동 상태(정책이 SetMode로 몰고 sim이 rate*tick
        // 적분). rate(초당)는 device 스펙(글로벌 기본값), tick은 status 보고 주기에서 주입.
        ddcs::agent::domain::SimulatedDevice::Config sim{};
        sim.tick_seconds =
            static_cast<double>(
                std::chrono::duration_cast<std::chrono::milliseconds>(cfg.session.status_update)
                    .count()
            ) /
            1000.0;
        sim.load_noise = env_double("DDCS_SIM_NOISE", sim.load_noise);
        sim.temp_noise = sim.load_noise * 0.5;
        // 개체차: load rate는 +-jitter(평균 보존), 초기 load/temp는 랜덤, noise seed도 device마다.
        // temp rate(발열/냉각)는 기기 공통 스펙이라 안 흔든다. 개체 분산은 초기온도+noise로만 줘서
        // device들이 high_temp에 서로 다른 시점에 닿게 한다(그래야 per-device thermal이 보임).
        std::mt19937_64 rng{std::random_device{}()};
        // jitter는 부호 보존 위해 (0,1): 1 이상이면 load_rate가 0/부호반전돼 limit cycle이 깨진다.
        // (음수/0은 아래 lambda가 그대로 통과시켜 무효화한다.)
        double jitter = env_double("DDCS_SIM_JITTER", 0.10);
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
        cfg.device = std::make_unique<ddcs::agent::domain::SimulatedDevice>(
            uuid.value, ddcs::device::Mode::normal, sim
        );
        if (auto const level =
                ddcs::logger::parse_level(conf.get_string("log.level", "DDCS_LOG_LEVEL", "info"))) {
            cfg.log_level = *level;
        }

        ddcs::agent::Agent agent{std::move(cfg)};
        agent.start();
        agent.run();
    } catch (std::exception const& e) {
        std::fprintf(stderr, "ddcs-agent: fatal: %s\n", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        std::fprintf(stderr, "ddcs-agent: fatal: unknown error\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
