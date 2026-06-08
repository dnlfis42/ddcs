#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/domain/simulated_device.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace {

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

// 표준 8-4-4-4-12 형식 (uuidgen 호환). 예: "0feef128-d17f-1f55-8565-9a23ddb8c29d"
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
    bool ephemeral; // env 지정이 아니라 부팅 시 random 생성됨
};

LoadedUuid load_agent_uuid() {
    char const* env = std::getenv("DDCS_AGENT_UUID");
    if (env != nullptr) {
        std::string_view const sv{env};
        if (auto u = parse_uuid_hex36(sv); u.has_value()) {
            return {*u, false};
        }
        if (auto u = parse_uuid_hex32(sv); u.has_value()) {
            return {*u, false};
        }
    }
    return {generate_random_uuid(), true};
}

std::string get_env_or(char const* name, std::string_view fallback) {
    char const* v = std::getenv(name);
    return v != nullptr ? std::string{v} : std::string{fallback};
}

std::uint16_t get_env_port_or(char const* name, std::uint16_t fallback) {
    char const* v = std::getenv(name);
    if (v == nullptr) {
        return fallback;
    }
    int const p = std::atoi(v);
    if (p <= 0 || p > 65535) {
        return fallback;
    }
    return static_cast<std::uint16_t>(p);
}

} // namespace

int main() {
    auto const uuid = load_agent_uuid();

    ddcs::agent::Agent::Config cfg{};
    cfg.controller_host = get_env_or("DDCS_CONTROLLER_HOST", "127.0.0.1");
    cfg.controller_port = get_env_port_or("DDCS_CONTROLLER_PORT", 8080);
    cfg.agent_uuid = uuid.value;
    cfg.agent_uuid_is_ephemeral = uuid.ephemeral;
    cfg.session.group = get_env_or("DDCS_AGENT_GROUP", "edge");
    cfg.device = std::make_unique<ddcs::agent::domain::SimulatedDevice>();
    if (char const* lvl = std::getenv("DDCS_LOG_LEVEL")) {
        cfg.log_level = ddcs::logger::level_from_string(lvl, cfg.log_level);
    }

    ddcs::agent::Agent agent{std::move(cfg)};
    agent.start();
    agent.run();
    return EXIT_SUCCESS;
}
