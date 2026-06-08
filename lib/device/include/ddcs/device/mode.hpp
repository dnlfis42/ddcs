#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace ddcs::device {

// 디바이스 작동 모드 - 공유 도메인 어휘(shared kernel).
// controller(정책/명령)와 agent(디바이스 제어)가 함께 쓰고, proto::cmd 가 와이어로 실어 나른다.
// 도메인 개념이므로 generic primitive 인 common 이 아니라 별도 device 에 둔다.
enum class Mode : std::uint8_t {
    safe = 0,
    normal = 1,
    performance = 2,
};

// Mode <-> 정규 문자열. config/policy 같은 텍스트 경계가 같은 이름을 쓰도록 여기서 공유.
// 분산 정의 시 "perf" vs "performance" 같은 drift 로 설정이 조용히 깨지는 걸 방지.
constexpr std::string_view to_string(Mode m) noexcept {
    switch (m) {
    case Mode::safe:
        return "safe";
    case Mode::normal:
        return "normal";
    case Mode::performance:
        return "performance";
    }
    return "safe"; // 유효 enum 엔 도달하지 않음
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
