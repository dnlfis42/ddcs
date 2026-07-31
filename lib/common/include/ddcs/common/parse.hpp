#pragma once

#include "ddcs/common/uuid.hpp"
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace ddcs::common {

// 문자열을 double로 파싱한다. 빈 문자열이나 "0.5abc"/"0.5 " 같은 부분 파싱은 nullopt.
[[nodiscard]] inline std::optional<double> parse_double(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    double value = 0.0;
    auto const* const end = text.data() + text.size();
    auto const [ptr, ec] = std::from_chars(text.data(), end, value);

    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

// 문자열을 TCP 포트(1..65535)로 파싱한다. 음수/빈 문자열/범위 밖은 nullopt.
// std::atoi와 달리 overflow UB가 없고 "8080abc"/"8080 " 같은 부분 파싱을 거부한다.
[[nodiscard]] inline std::optional<std::uint16_t> parse_port(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    std::uint16_t value = 0;
    auto const* const end = text.data() + text.size();
    auto const [ptr, ec] = std::from_chars(text.data(), end, value);

    if (ec != std::errc{} || ptr != end || value == 0) {
        return std::nullopt;
    }
    return value;
}

// 문자열을 신원용 Uuid로 파싱한다. 형식 위반과 nil(무의미 신원)은 nullopt.
[[nodiscard]] inline std::optional<Uuid> parse_uuid(std::string_view text) noexcept {
    auto const u = Uuid::parse(text);
    if (!u || u->is_nil()) {
        return std::nullopt;
    }
    return u;
}

} // namespace ddcs::common
