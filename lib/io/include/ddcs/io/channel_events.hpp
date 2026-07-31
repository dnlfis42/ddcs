#pragma once

#include <cstdint>

namespace ddcs::io {

enum class ChannelEvents : std::uint32_t {
    none = 0,
    readable = 1u << 0,        // 읽을 게 있다.
    writable = 1u << 1,        // 쓸 수 있다.
    error = 1u << 2,           // 소켓 오류
    hangup = 1u << 3,          // 상대가 끊음 (FIN/RST)
    edge_triggered = 1u << 16, // ET 모드
    one_shot = 1u << 17,       // 1회성
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

[[nodiscard]] constexpr bool contains(ChannelEvents mask, ChannelEvents bits) noexcept {
    return (to_underlying(mask) & to_underlying(bits)) == to_underlying(bits);
}

} // namespace ddcs::io
