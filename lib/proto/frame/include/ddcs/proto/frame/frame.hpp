#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace ddcs::proto::frame {

// DDCS frame 헤더: [magic(BE u16) | type(u8) | payload_size(BE u16)]
inline constexpr std::uint16_t magic{0xDDC5};
inline constexpr std::size_t header_size{5};
inline constexpr std::size_t payload_size_limit{std::numeric_limits<std::uint16_t>::max()};

struct Header {
    std::uint16_t magic;
    std::uint8_t type;
    std::uint16_t payload_size;

    bool operator==(Header const&) const = default;
};

using HeaderBytes = std::array<std::byte, header_size>;

// decode는 byte layout만 풀고, parse는 protocol magic까지 검증한다.
HeaderBytes encode(Header const& header) noexcept;
Header decode(HeaderBytes const& bytes) noexcept;
std::optional<Header> parse(HeaderBytes const& bytes) noexcept;

} // namespace ddcs::proto::frame
