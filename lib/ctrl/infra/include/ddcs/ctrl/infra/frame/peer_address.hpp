#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ddcs::ctrl::infra::frame {

inline constexpr std::size_t min_peer_address_format_buffer_size{64};

struct PeerAddress {
    enum class Family : std::uint8_t { none, v4, v6 };

    Family family{Family::none};
    std::uint16_t port{};
    std::array<std::uint8_t, 16> addr{};

    bool operator==(PeerAddress const&) const = default;

    [[nodiscard]] std::string_view format(std::span<char> buf) const noexcept;

    void clear() noexcept {
        family = Family::none;
        port = 0;
        addr = {};
    }

    void reset() noexcept {
        clear();
    }
};

} // namespace ddcs::ctrl::infra::frame
