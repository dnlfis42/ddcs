#pragma once

#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ddcs::device {

// Agent와 Controller가 공유하는 device command 어휘
enum class CommandType : std::uint8_t {
    invalid = 0x00,
    set_mode = 0x01,
};

struct SetMode {
    Mode mode;
};

// NOTE: 두 함수는 Command envelope가 아니라 device command payload만 다룬다.
[[nodiscard]] std::optional<std::size_t>
encode_set_mode(std::span<std::byte> out, Mode mode) noexcept;
[[nodiscard]] std::optional<SetMode> decode_set_mode(std::span<std::byte const> in) noexcept;

} // namespace ddcs::device
