#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ddcs::device {

// Agent와 Controller가 공유하는 device command 어휘
enum class CommandType : std::uint8_t {
    set_mode = 0x01,
};

struct SetMode {
    Mode mode;

    bool operator==(SetMode const&) const = default;
};

template <typename T>
inline constexpr CommandType type_of = T::__type_specialization_missing;

template <>
inline constexpr CommandType type_of<SetMode> = CommandType::set_mode;

// CAUTION: encode/decode는 Command envelope가 아니라 device command payload만 다룬다.
bool encode(SetMode const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, SetMode&) noexcept;

} // namespace ddcs::device
