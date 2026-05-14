#pragma once

#include <array>
#include <limits>

#include <cstddef>
#include <cstdint>

namespace ddcs::protocol {

inline constexpr std::uint16_t magic{0xDDC5};
inline constexpr std::size_t header_size{6};
inline constexpr std::size_t max_payload{std::numeric_limits<std::uint16_t>::max()};

struct Header {
    std::uint16_t magic;
    std::uint8_t version;
    std::uint8_t opcode;
    std::uint16_t length;

    bool operator==(Header const&) const = default;
};

using HeaderBytes = std::array<std::byte, header_size>;

HeaderBytes encode_header(Header const& hdr);
Header decode_header(HeaderBytes const& src);

} // namespace ddcs::protocol
