#include "ddcs/proto/frame/frame.hpp"

#include "ddcs/common/endian.hpp"

#include <cstring>

namespace ddcs::proto::frame {

namespace {

constexpr std::size_t magic_offset{0};
constexpr std::size_t type_offset{2};
constexpr std::size_t length_offset{3};

} // namespace

HeaderBytes encode(Header const& header) noexcept {
    HeaderBytes bytes{};
    auto const encoded_magic = common::to_be(header.magic);
    auto const encoded_length = common::to_be(header.length);
    std::memcpy(bytes.data() + magic_offset, &encoded_magic, sizeof(encoded_magic));
    bytes[type_offset] = static_cast<std::byte>(header.type);
    std::memcpy(bytes.data() + length_offset, &encoded_length, sizeof(encoded_length));
    return bytes;
}

Header decode(HeaderBytes const& bytes) noexcept {
    std::uint16_t magic_raw{};
    std::uint16_t length_raw{};
    std::memcpy(&magic_raw, bytes.data() + magic_offset, sizeof(magic_raw));
    std::memcpy(&length_raw, bytes.data() + length_offset, sizeof(length_raw));
    return Header{
        .magic = common::from_be(magic_raw),
        .type = static_cast<std::uint8_t>(bytes[type_offset]),
        .length = common::from_be(length_raw),
    };
}

std::optional<Header> parse(HeaderBytes const& bytes) noexcept {
    Header const header = decode(bytes);
    if (header.magic != magic) {
        return std::nullopt;
    }
    return header;
}

} // namespace ddcs::proto::frame
