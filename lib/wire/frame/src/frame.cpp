#include "ddcs/wire/frame/frame.hpp"

#include "ddcs/common/endian.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace ddcs::wire::frame {

namespace {

constexpr std::size_t magic_offset = 0;
constexpr std::size_t payload_length_offset = 2;

} // namespace

HeaderBytes encode(std::uint16_t payload_length) noexcept {
    HeaderBytes bytes{};
    auto const encoded_magic = common::to_be(magic_value);
    auto const encoded_length = common::to_be(payload_length);
    std::memcpy(bytes.data() + magic_offset, &encoded_magic, sizeof(encoded_magic));
    std::memcpy(bytes.data() + payload_length_offset, &encoded_length, sizeof(encoded_length));
    return bytes;
}

std::optional<std::uint16_t> decode(HeaderBytes const& bytes) noexcept {
    std::uint16_t magic_raw{};
    std::uint16_t length_raw{};
    std::memcpy(&magic_raw, bytes.data() + magic_offset, sizeof(magic_raw));
    std::memcpy(&length_raw, bytes.data() + payload_length_offset, sizeof(length_raw));

    if (common::from_be(magic_raw) != magic_value) {
        return std::nullopt;
    }
    return common::from_be(length_raw);
}

} // namespace ddcs::wire::frame
