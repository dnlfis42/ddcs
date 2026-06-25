#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace ddcs::device {

// Agent와 Controller가 공유하는 device mode 어휘
enum class Mode : std::uint8_t {
    safe = 0,
    normal = 1,
    performance = 2,
};

// config/policy 같은 텍스트 경계가 같은 mode 이름을 쓰도록 여기서 정규화한다.
constexpr std::string_view to_string(Mode m) noexcept {
    switch (m) {
    case Mode::safe:
        return "safe";
    case Mode::normal:
        return "normal";
    case Mode::performance:
        return "performance";
    }
    return {}; // 어휘 밖 값(버그/손상)은 빈 문자열로 노출
}

// 텍스트를 mode 어휘로 검증해 해석한다.
// - 어휘 밖 값은 nullopt
constexpr std::optional<Mode> parse_mode(std::string_view text) noexcept {
    if (text == "safe") {
        return Mode::safe;
    }
    if (text == "normal") {
        return Mode::normal;
    }
    if (text == "performance") {
        return Mode::performance;
    }
    return std::nullopt;
}

// mode 어휘를 wire byte로 직렬화한다.
constexpr std::uint8_t encode_mode(Mode m) noexcept {
    switch (m) {
    case Mode::safe:
        return 0;
    case Mode::normal:
        return 1;
    case Mode::performance:
        return 2;
    }
    return 0; // 어휘 밖(불가) 방어
}

// wire byte를 mode 어휘로 검증해 해석한다.
// - 어휘 밖 값은 nullopt
constexpr std::optional<Mode> decode_mode(std::uint8_t wire) noexcept {
    switch (wire) {
    case 0:
        return Mode::safe;
    case 1:
        return Mode::normal;
    case 2:
        return Mode::performance;
    default:
        return std::nullopt;
    }
}

} // namespace ddcs::device
