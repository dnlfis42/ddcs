#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ddcs::proto::cmd {

// CAUTION: 값은 msg::Command.type의 wire 값이다. 기존 값은 재사용하지 않는다.
enum class CommandType : std::uint8_t {
    set_mode = 0x01,
};

struct SetMode {
    device::Mode mode;

    bool operator==(SetMode const&) const = default;
};

template <typename T>
inline constexpr CommandType type_of = T::__type_specialization_missing;
template <>
inline constexpr CommandType type_of<SetMode> = CommandType::set_mode;

// CAUTION: payload는 msg::Command.type 뒤의 남은 바이트만 다룬다. command_id/type 바이트는 포함하지 않는다.
// SetMode payload는 [mode(u8)] 형식.
bool encode(SetMode const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, SetMode&) noexcept;

} // namespace ddcs::proto::cmd
