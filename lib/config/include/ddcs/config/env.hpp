#pragma once

#include "ddcs/common/parse.hpp"
#include "ddcs/logger/event.hpp"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace ddcs::config::env {

// 환경변수를 읽는다. 미설정이면 nullopt
[[nodiscard]] inline std::optional<std::string_view> get(char const* name) noexcept {
    char const* const v = std::getenv(name);
    if (v == nullptr) {
        return std::nullopt;
    }
    return std::string_view{v};
}

// 환경변수를 double로 읽는다. 미설정은 조용히, 설정됐는데 무효(오타)면 경고 후 nullopt
[[nodiscard]] inline std::optional<double> get_double(char const* name) noexcept {
    auto const v = get(name);
    if (!v) {
        return std::nullopt;
    }
    auto const parsed = common::parse_double(*v);
    if (!parsed) {
        LOG_CONFIG_VALUE_INVALID("env", name, "number", *v);
    }
    return parsed;
}

// 층 쌓기용: 미설정이면 조용히, 무효면 경고 후 fallback(직전 층 값) 유지
[[nodiscard]] inline double get_double(char const* name, double fallback) noexcept {
    return get_double(name).value_or(fallback);
}

// 환경변수를 TCP 포트로 읽는다. 미설정은 조용히, 설정됐는데 무효(오타)면 경고 후 nullopt
[[nodiscard]] inline std::optional<std::uint16_t> get_port(char const* name) noexcept {
    auto const v = get(name);
    if (!v) {
        return std::nullopt;
    }
    auto const port = common::parse_port(*v);
    if (!port) {
        LOG_CONFIG_VALUE_INVALID("env", name, "port (1..65535)", *v);
    }
    return port;
}

// 층 쌓기용: 미설정이면 조용히, 무효면 경고 후 fallback(직전 층 값) 유지
[[nodiscard]] inline std::uint16_t get_port(char const* name, std::uint16_t fallback) noexcept {
    return get_port(name).value_or(fallback);
}

} // namespace ddcs::config::env
