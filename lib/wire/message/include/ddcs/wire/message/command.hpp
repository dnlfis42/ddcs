#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ddcs::wire::message {

// Agent와 Controller가 공유하는 device control command 어휘
enum class CommandType : std::uint8_t {
    invalid = 0x00,
    set_mode = 0x01,
};

struct SetMode {
    std::uint8_t mode;
};

[[nodiscard]] std::optional<std::size_t>
encode_set_mode(std::span<std::byte> out, std::uint8_t mode) noexcept;
[[nodiscard]] std::optional<SetMode> decode_set_mode(std::span<std::byte const> in) noexcept;

} // namespace ddcs::wire::message
