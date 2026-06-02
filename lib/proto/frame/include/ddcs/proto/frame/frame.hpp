#pragma once

#include <array>
#include <limits>

#include <cstddef>
#include <cstdint>

namespace ddcs::proto::frame {

// wire: [ magic(BE u16) | type(u8) | length(BE u16) ] = 5 byte
inline constexpr std::uint16_t magic{0xDDC5};
inline constexpr std::size_t header_size{5};
inline constexpr std::size_t max_payload{std::numeric_limits<std::uint16_t>::max()};

struct Header {
    std::uint16_t magic;  // 고정값0xDDC5
    std::uint8_t type;    // opaque 값
    std::uint16_t length; // payload 바이트 수 (header 미포함)

    bool operator==(Header const&) const = default;
};

using HeaderBytes = std::array<std::byte, header_size>;

HeaderBytes encode(Header const& hdr);
Header decode(HeaderBytes const& src);

} // namespace ddcs::proto::frame
