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
    return "safe"; // 유효 enum에는 도달하지 않음
}

constexpr std::optional<Mode> from_string(std::string_view s) noexcept {
    if (s == "safe") {
        return Mode::safe;
    }
    if (s == "normal") {
        return Mode::normal;
    }
    if (s == "performance") {
        return Mode::performance;
    }
    return std::nullopt;
}

} // namespace ddcs::device
