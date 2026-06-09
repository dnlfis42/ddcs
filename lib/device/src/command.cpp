#include "ddcs/device/command.hpp"
#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ddcs::device {

bool encode(const SetMode& cmd, common::LinearBuffer& out) noexcept {
    std::byte const mode_b{static_cast<std::uint8_t>(cmd.mode)};
    return out.write({&mode_b, 1});
}
bool decode(std::span<std::byte const> in, SetMode& out) noexcept {
    if (in.size() != 1) {
        return false;
    }
    out.mode = static_cast<device::Mode>(static_cast<std::uint8_t>(in[0]));
    return true;
}

} // namespace ddcs::device
