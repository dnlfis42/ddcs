#include "ddcs/proto/frame/frame.hpp"

#include "ddcs/common/endian.hpp"

#include <cstring>

namespace ddcs::proto::frame {

HeaderBytes encode(Header const& hdr) {
    HeaderBytes out{};
    auto const m = common::to_be(hdr.magic);
    auto const l = common::to_be(hdr.length);
    std::memcpy(out.data() + 0, &m, sizeof(m)); // magic @0..1 (BE)
    out[2] = static_cast<std::byte>(hdr.type);  // type  @2    (opaque)
    std::memcpy(out.data() + 3, &l, sizeof(l)); // len   @3..4 (BE)
    return out;
}

Header decode(HeaderBytes const& src) {
    std::uint16_t magic_raw{};
    std::uint16_t length_raw{};
    std::memcpy(&magic_raw, src.data() + 0, sizeof(magic_raw));
    std::memcpy(&length_raw, src.data() + 3, sizeof(length_raw));
    return Header{
        .magic = common::from_be(magic_raw),
        .type = static_cast<std::uint8_t>(src[2]),
        .length = common::from_be(length_raw),
    };
}

} // namespace ddcs::proto::frame
