#include "ddcs/net/frame/frame.hpp"

#include "ddcs/net/codec/endian.hpp"

#include <cstring>

namespace ddcs::net::frame {

HeaderBytes encode(Header const& hdr) {
    HeaderBytes out{};
    auto const m = codec::to_be(hdr.magic);
    auto const l = codec::to_be(hdr.length);
    std::memcpy(out.data() + 0, &m, sizeof(m));
    out[2] = static_cast<std::byte>(hdr.version);
    out[3] = static_cast<std::byte>(hdr.opcode);
    std::memcpy(out.data() + 4, &l, sizeof(l));
    return out;
}

Header decode(HeaderBytes const& src) {
    std::uint16_t magic_raw{};
    std::uint16_t length_raw{};
    std::memcpy(&magic_raw, src.data() + 0, sizeof(magic_raw));
    std::memcpy(&length_raw, src.data() + 4, sizeof(length_raw));
    return Header{
        .magic = codec::from_be(magic_raw),
        .version = std::to_integer<std::uint8_t>(src[2]),
        .opcode = std::to_integer<std::uint8_t>(src[3]),
        .length = codec::from_be(length_raw),
    };
}

} // namespace ddcs::net::frame
