#include "ddcs/wire/command/command.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace ddcs::wire::command {

std::optional<std::size_t> encode_set_mode(std::span<std::byte> out, std::uint8_t mode) noexcept {
    if (out.empty()) {
        return std::nullopt;
    }

    out[0] = std::byte{mode};
    return 1;
}

std::optional<SetMode> decode_set_mode(std::span<std::byte const> in) noexcept {
    if (in.size() != 1) {
        return std::nullopt;
    }

    return SetMode{.mode = static_cast<std::uint8_t>(in[0])};
}

} // namespace ddcs::wire::command
