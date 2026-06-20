#include "ddcs/device/command.hpp"
#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ddcs::device {

std::optional<std::size_t> encode_set_mode(std::span<std::byte> out, Mode mode) noexcept {
    if (out.empty()) {
        return std::nullopt;
    }

    out[0] = std::byte{static_cast<std::uint8_t>(mode)};
    return 1;
}

std::optional<SetMode> decode_set_mode(std::span<std::byte const> in) noexcept {
    if (in.size() != 1) {
        return std::nullopt;
    }

    auto const mode = decode_mode(static_cast<std::uint8_t>(in[0]));
    if (!mode) {
        return std::nullopt;
    }
    return SetMode{.mode = *mode};
}

} // namespace ddcs::device
