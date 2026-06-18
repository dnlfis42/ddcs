#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ddcs::wire::frame {

inline constexpr std::uint16_t magic_value{0xDDC5};
inline constexpr std::size_t header_size{4};

// Layout (big-endian):
//   [0, 2) : magic (0xDDC5)
//   [2, 4) : payload_length
using HeaderBytes = std::array<std::byte, header_size>;

[[nodiscard]] HeaderBytes encode(std::uint16_t payload_length) noexcept;

// magic을 검증하고 payload_length를 반환한다. magic 불일치면 nullopt.
// 길이 정책(버퍼 한계 등) 검사는 호출측이 한다.
[[nodiscard]] std::optional<std::uint16_t> decode(HeaderBytes const& bytes) noexcept;

} // namespace ddcs::wire::frame
