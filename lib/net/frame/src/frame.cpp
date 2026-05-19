#include "ddcs/net/frame/frame.hpp"

namespace ddcs::net::frame {

HeaderBytes encode_header(Header const& hdr) {
    return {
        std::byte((hdr.magic >> 8) & 0xff),
        std::byte(hdr.magic & 0xff),
        std::byte(hdr.version),
        std::byte(hdr.opcode),
        std::byte((hdr.length >> 8) & 0xff),
        std::byte(hdr.length & 0xff),
    };
}

Header decode_header(HeaderBytes const& src) {
    return {
        .magic = static_cast<std::uint16_t>(
            (std::to_integer<std::uint16_t>(src[0]) << 8) | std::to_integer<std::uint16_t>(src[1])
        ),
        .version = std::to_integer<std::uint8_t>(src[2]),
        .opcode = std::to_integer<std::uint8_t>(src[3]),
        .length = static_cast<std::uint16_t>(
            (std::to_integer<std::uint16_t>(src[4]) << 8) | std::to_integer<std::uint16_t>(src[5])
        ),
    };
}

} // namespace ddcs::net::frame
