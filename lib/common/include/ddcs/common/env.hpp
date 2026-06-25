#pragma once

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace ddcs::common {

// 문자열을 TCP 포트(1..65535)로 파싱한다.
// - 전체 소비(후행 문자 거부), 음수/빈 문자열/범위 밖은 nullopt
// - std::atoi와 달리 overflow UB가 없고 "8080abc"/"8080 " 같은 부분 파싱을 거부한다.
[[nodiscard]] inline std::optional<std::uint16_t> parse_port(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint16_t value = 0;
    auto const* const end = text.data() + text.size();
    auto const [ptr, ec] = std::from_chars(text.data(), end, value);
    // ec != {} 면 파싱 실패/음수(invalid_argument)/범위 초과(result_out_of_range)
    // ptr != end 면 후행 garbage, value == 0 이면 포트로 무의미
    if (ec != std::errc{} || ptr != end || value == 0) {
        return std::nullopt;
    }
    return value;
}

// 환경변수 name을 포트로 읽는다. 미설정/유효하지 않으면 fallback(결정적, UB 없음).
[[nodiscard]] inline std::uint16_t get_env_port_or(char const* name, std::uint16_t fallback) {
    char const* const v = std::getenv(name);
    if (v == nullptr) {
        return fallback;
    }
    return parse_port(v).value_or(fallback);
}

} // namespace ddcs::common
