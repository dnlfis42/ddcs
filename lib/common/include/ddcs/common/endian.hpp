#pragma once

#include <bit>
#include <concepts>

namespace ddcs::common {

namespace detail::endian {

template <typename T>
concept byteswappable_unsigned =
    std::unsigned_integral<T> && !std::same_as<T, bool> &&
    (sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);

} // namespace detail::endian

template <detail::endian::byteswappable_unsigned T>
[[nodiscard]] constexpr T byteswap(T v) noexcept {
    if constexpr (sizeof(T) == 1) {
        return v;
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(
            ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) |
            ((v & 0xFF000000u) >> 24)
        );
    } else {
        return static_cast<T>(
            ((v & 0x00000000000000FFull) << 56) | ((v & 0x000000000000FF00ull) << 40) |
            ((v & 0x0000000000FF0000ull) << 24) | ((v & 0x00000000FF000000ull) << 8) |
            ((v & 0x000000FF00000000ull) >> 8) | ((v & 0x0000FF0000000000ull) >> 24) |
            ((v & 0x00FF000000000000ull) >> 40) | ((v & 0xFF00000000000000ull) >> 56)
        );
    }
}

template <detail::endian::byteswappable_unsigned T>
[[nodiscard]] constexpr T to_be(T v) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return v;
    } else {
        return byteswap(v);
    }
}

template <detail::endian::byteswappable_unsigned T>
[[nodiscard]] constexpr T to_le(T v) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return v;
    } else {
        return byteswap(v);
    }
}

template <detail::endian::byteswappable_unsigned T>
[[nodiscard]] constexpr T from_be(T v) noexcept {
    return to_be(v);
}

template <detail::endian::byteswappable_unsigned T>
[[nodiscard]] constexpr T from_le(T v) noexcept {
    return to_le(v);
}

} // namespace ddcs::common
