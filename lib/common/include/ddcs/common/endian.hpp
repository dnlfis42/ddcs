#pragma once

#include <bit>
#include <concepts>

namespace ddcs::common {

template <std::unsigned_integral T>
constexpr T byteswap(T v) noexcept {
    static_assert(
        sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
        "byteswap supports only 1, 2, 4, and 8 byte unsigned integers"
    );

    if constexpr (sizeof(T) == 1) {
        return v;
    } else if constexpr (sizeof(T) == 2) {
        return static_cast<T>(((v & 0x00FFu) << 8) | ((v & 0xFF00u) >> 8));
    } else if constexpr (sizeof(T) == 4) {
        return static_cast<T>(
            ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24)
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

template <std::unsigned_integral T>
constexpr T to_be(T v) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return v;
    } else {
        return byteswap(v);
    }
}

template <std::unsigned_integral T>
constexpr T to_le(T v) noexcept {
    if constexpr (std::endian::native == std::endian::little) {
        return v;
    } else {
        return byteswap(v);
    }
}

template <std::unsigned_integral T>
constexpr T from_be(T v) noexcept {
    return to_be(v);
}

template <std::unsigned_integral T>
constexpr T from_le(T v) noexcept {
    return to_le(v);
}

} // namespace ddcs::common
