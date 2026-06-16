#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ddcs::common {

class Uuid {
public:
    static constexpr std::array<std::byte, 16> invalid{};

public:
    constexpr Uuid() noexcept = default;
    constexpr explicit Uuid(std::array<std::byte, 16> const& bytes) noexcept
        : bytes_{bytes} {}

    std::array<std::byte, 16> const& bytes() const noexcept {
        return bytes_;
    }

    constexpr bool valid() const noexcept {
        return bytes_ != invalid;
    }

    constexpr bool operator==(Uuid const&) const noexcept = default;
    constexpr auto operator<=>(Uuid const&) const noexcept = default;

    std::string to_string() const {
        constexpr char hex[] = "0123456789abcdef";
        std::string out(36, '-');
        std::size_t pos = 0;
        for (std::size_t i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                ++pos;
            }
            auto const b = static_cast<std::uint8_t>(bytes_[i]);
            out[pos++] = hex[(b >> 4) & 0x0F];
            out[pos++] = hex[b & 0x0F];
        }
        return out;
    }

    constexpr void clear() noexcept {
        bytes_ = invalid;
    }

private:
    std::array<std::byte, 16> bytes_{};
};

} // namespace ddcs::common

template <>
struct std::hash<ddcs::common::Uuid> {
    std::size_t operator()(ddcs::common::Uuid const& uuid) const noexcept {
        auto const pair = std::bit_cast<std::array<std::uint64_t, 2>>(uuid.bytes());
        return pair[0] ^ pair[1];
    }
};
