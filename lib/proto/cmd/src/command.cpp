#include "ddcs/proto/cmd/command.hpp"

#include <cstddef>
#include <cstdint>

namespace ddcs::proto::cmd {

bool encode(SetMode const& m, common::LinearBuffer& out) noexcept {
    std::byte const mode_b{static_cast<std::uint8_t>(m.mode)};
    return out.write({&mode_b, 1});
}
bool decode(std::span<std::byte const> in, SetMode& out) noexcept {
    if (in.size() != 1) {
        return false;
    }
    out.mode = static_cast<device::Mode>(static_cast<std::uint8_t>(in[0]));
    return true;
}

} // namespace ddcs::proto::cmd
