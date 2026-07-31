#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace ddcs::wire::command {

// Agent와 Controller가 공유하는 device control command 어휘
enum class CommandType : std::uint8_t {
    invalid = 0x00,
    set_mode = 0x01,
};

struct SetMode {
    std::uint8_t mode;
};

// 명령 계열. 새 명령은 struct + variant 대안 + codec을 함께 늘린다.
// mode 같은 도메인 어휘와 wire byte 간 매핑은 발행측과 수신측이 device 커널로 마친다.
using Command = std::variant<SetMode>;

[[nodiscard]] std::optional<std::size_t>
encode_set_mode(std::span<std::byte> out, std::uint8_t mode) noexcept;
[[nodiscard]] std::optional<SetMode> decode_set_mode(std::span<std::byte const> in) noexcept;

} // namespace ddcs::wire::command
