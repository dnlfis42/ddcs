#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace ddcs::wire::frame {

inline constexpr std::size_t header_size = 4;
inline constexpr std::uint16_t magic_value = 0xDDC5;
inline constexpr std::size_t max_payload_length = 1024;

// Layout:
// - [0, 2) : magic (big-endian)
// - [2, 4) : payload_length (big-endian)
using HeaderBytes = std::array<std::byte, header_size>;

[[nodiscard]] HeaderBytes encode(std::uint16_t payload_length) noexcept;

// 헤더를 검증하고 payload_length를 반환한다.
// magic 불일치면 nullopt
[[nodiscard]] std::optional<std::uint16_t> decode(HeaderBytes const& bytes) noexcept;

} // namespace ddcs::wire::frame
