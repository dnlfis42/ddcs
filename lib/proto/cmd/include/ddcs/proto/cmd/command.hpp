#pragma once

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ddcs::proto::cmd {

// 명령 종류 - msg::Command.type에 실리는 opaque 바이트의 의미.
enum class CommandType : std::uint8_t {
    SetMode = 0x01,
};

struct SetMode {
    device::Mode mode; // 공유 도메인 어휘 - ddcs::device::Mode

    bool operator==(SetMode const&) const = default;
};

// -> CommandType 매핑 (msg::Command.type 결정)
template <typename T>
inline constexpr CommandType type_of = T::__type_specialization_missing;
template <>
inline constexpr CommandType type_of<SetMode> = CommandType::SetMode;

// payload codec - msg::Command.payload <-> 명령 struct.
// (command_id와 type 바이트는 msg::Command 소관. 여기는 payload 본문만 다룬다.)
//   SetMode: [mode(u8)]
bool encode(SetMode const&, common::LinearBuffer&) noexcept;
bool decode(std::span<std::byte const>, SetMode&) noexcept;

} // namespace ddcs::proto::cmd
