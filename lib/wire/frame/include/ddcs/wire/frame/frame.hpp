#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace ddcs::wire::frame {

inline constexpr std::uint16_t magic_value{0xDDC5};
inline constexpr std::size_t encoded_header_size{4};
inline constexpr std::size_t max_encodable_payload_size{std::numeric_limits<std::uint16_t>::max()};

struct Header {
    std::uint16_t magic;
    std::uint16_t payload_length;

    bool operator==(Header const&) const = default;
};

using HeaderBytes = std::array<std::byte, encoded_header_size>;

// NOTE: decode는 byte layout만 풀고, parse는 protocol magic까지 검증한다.
[[nodiscard]] HeaderBytes encode(std::uint16_t payload_length) noexcept;
[[nodiscard]] Header decode(HeaderBytes const& bytes) noexcept;
[[nodiscard]] std::optional<Header> parse(HeaderBytes const& bytes) noexcept;

} // namespace ddcs::wire::frame
