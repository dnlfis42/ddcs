#pragma once

#include <cstdint>

namespace ddcs::io {

enum class ChannelEvents : std::uint32_t {
    none = 0,
    readable = 1u << 0,
    writable = 1u << 1,
    error = 1u << 2,
    hangup = 1u << 3,

    edge_triggered = 1u << 16,
    one_shot = 1u << 17,
};

[[nodiscard]] constexpr std::uint32_t to_underlying(ChannelEvents mask) noexcept {
    return static_cast<std::uint32_t>(mask);
}

[[nodiscard]] constexpr ChannelEvents operator|(ChannelEvents lhs, ChannelEvents rhs) noexcept {
    return static_cast<ChannelEvents>(to_underlying(lhs) | to_underlying(rhs));
}

constexpr ChannelEvents& operator|=(ChannelEvents& lhs, ChannelEvents rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr ChannelEvents operator&(ChannelEvents lhs, ChannelEvents rhs) noexcept {
    return static_cast<ChannelEvents>(to_underlying(lhs) & to_underlying(rhs));
}

[[nodiscard]] constexpr bool any(ChannelEvents mask) noexcept {
    return mask != ChannelEvents::none;
}

[[nodiscard]] constexpr bool contains(ChannelEvents mask, ChannelEvents bits) noexcept {
    return (to_underlying(mask) & to_underlying(bits)) == to_underlying(bits);
}

} // namespace ddcs::io
