#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/domain/simulated_device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/config/config.hpp"
#include "ddcs/logger/log.hpp"

#include <array>
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
// 1. DDCS_AGENT_UUID (env, 명시)
// 2. DDCS_AGENT_UUID_FILE (기본 data/agent.uuid) 읽기 - 재시작에도 안정
// 3. 랜덤 생성 후 그 파일에 기록 - 첫 부팅 자가발급(이후 안정). 기록 실패 시에만 ephemeral.
LoadedUuid load_device_uuid() {
    if (char const* env = std::getenv("DDCS_AGENT_UUID")) {
        if (auto u = parse_device_uuid(env)) {
            return {*u, false};
        }
        std::fprintf(stderr, "ddcs-agent: DDCS_AGENT_UUID is invalid or nil; ignoring\n");
    }

    std::filesystem::path const path = env_or("DDCS_AGENT_UUID_FILE", "data/agent.uuid");

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
        std::filesystem::path const dir = env_or("DDCS_CONFIG_DIR", "config");
        ddcs::config::Config conf;
        load_or_note(conf, dir / "network.json"); // 공유 엔드포인트
        load_or_note(conf, dir / "agent.json");   // agent 전용(uuid 없음)

        auto const uuid = load_device_uuid();

        ddcs::agent::Agent::Config cfg{};
        cfg.controller_host =
            conf.get_string("controller.host", "DDCS_CONTROLLER_HOST", "127.0.0.1");
        cfg.controller_port = conf.get_port("controller.port", "DDCS_CONTROLLER_PORT", 8080);
        cfg.device_id_is_ephemeral = uuid.ephemeral;
        cfg.session.group = conf.get_string("device.group", "DDCS_AGENT_GROUP", "edge");
        cfg.session.heartbeat = conf.get_duration_ms("session_ms.heartbeat", 1000);
        cfg.session.status_update = conf.get_duration_ms("session_ms.status_update", 5000);
        cfg.session.register_timeout = conf.get_duration_ms("session_ms.register_timeout", 2000);
        cfg.device = std::make_unique<ddcs::agent::domain::SimulatedDevice>(uuid.value);
        if (auto const level =
                ddcs::logger::parse_level(conf.get_string("log_level", "DDCS_LOG_LEVEL", "info"))) {
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
