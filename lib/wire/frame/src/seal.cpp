#include "ddcs/wire/frame/seal.hpp"

#include "ddcs/wire/frame/frame.hpp"

#include <cstdint>

namespace ddcs::wire::frame {

bool reserve_header_room(common::LinearBuffer& message) noexcept {
    return message.try_grow_headroom(header_size);
}

bool seal(common::LinearBuffer& message) noexcept {
    if (message.size() > max_payload_length) {
        return false;
    }
    auto const header = encode(static_cast<std::uint16_t>(message.size()));
    return message.try_prepend(header);
}

} // namespace ddcs::wire::frame
