#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace ddcs::common {

class Uuid {
public:
    static constexpr std::array<std::byte, 16> nil{};

    constexpr Uuid() noexcept = default;
    constexpr explicit Uuid(std::array<std::byte, 16> const& bytes) noexcept
        : bytes_(bytes) {}

    [[nodiscard]] constexpr std::array<std::byte, 16> const& bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return bytes_ != nil;
    }

    [[nodiscard]] constexpr bool is_nil() const noexcept {
        return bytes_ == nil;
    }

    // 32자리 hex 또는 canonical 8-4-4-4-12 문자열을 파싱한다. 형식 위반이면 nullopt (nil은 허용)
    [[nodiscard]] static std::optional<Uuid> parse(std::string_view text) noexcept {
        if (text.size() == 36) {
            if (text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
                return std::nullopt;
            }
        } else if (text.size() != 32) {
            return std::nullopt;
        }

        std::array<std::byte, 16> bytes{};
        std::size_t nibble = 0;
        for (char const c : text) {
            if (c == '-') {
                continue;
            }
            int const h = hex_value(c);
            if (h < 0) {
                return std::nullopt;
            }
            auto const shifted = static_cast<std::uint8_t>(nibble % 2 == 0 ? h << 4 : h);
            bytes[nibble / 2] = std::byte{
                static_cast<std::uint8_t>(static_cast<std::uint8_t>(bytes[nibble / 2]) | shifted)
            };
            ++nibble;
        }
        if (nibble != 32) {
            return std::nullopt; // dash가 제자리에 있어도 hex가 모자라면 거부
        }
        return Uuid{bytes};
    }

    // 무작위 Uuid를 만든다 (신원 자가발급용)
    [[nodiscard]] static Uuid random() {
        std::random_device rd;
        // random_device 1회는 32bit라 두 번 뽑아 64bit 시드를 채운다
        std::mt19937_64 gen{(static_cast<std::uint64_t>(rd()) << 32) | rd()};
        std::array<std::byte, 16> bytes{};
        for (auto& b : bytes) {
            b = std::byte{static_cast<std::uint8_t>(gen() & 0xff)};
        }
        return Uuid{bytes};
    }

    // Canonical UUID 문자열(8-4-4-4-12 소문자 hex)로 변환한다.
    [[nodiscard]] std::string to_string() const {
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
        bytes_ = nil;
    }

    constexpr bool operator==(Uuid const&) const noexcept = default;
    constexpr auto operator<=>(Uuid const&) const noexcept = default;

private:
    [[nodiscard]] static constexpr int hex_value(char c) noexcept {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }

    std::array<std::byte, 16> bytes_ = nil;
};

} // namespace ddcs::common

template <>
struct std::hash<ddcs::common::Uuid> {
    std::size_t operator()(ddcs::common::Uuid const& uuid) const noexcept {
        auto const pair = std::bit_cast<std::array<std::uint64_t, 2>>(uuid.bytes());
        return pair[0] ^ pair[1];
    }
};
