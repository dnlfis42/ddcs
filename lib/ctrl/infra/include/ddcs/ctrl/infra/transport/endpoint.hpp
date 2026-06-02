#pragma once

#include <array>
#include <span>
#include <string_view>

#include <cstddef>
#include <cstdint>

namespace ddcs::ctrl::infra::transport {

inline constexpr std::size_t endpoint_format_min_size{64};

struct Endpoint {
    enum class Family : std::uint8_t { none, v4, v6 };

    Family family{Family::none};
    std::uint16_t port{};
    // 네트워크 바이트 순서의 주소 옥텟
    // family == v4 : 앞 4 B(addr[0..3])만 유효. 나머지는 0.
    // family == v6 : 16 B 전부 사용.
    std::array<std::uint8_t, 16> addr{};

    bool operator==(Endpoint const&) const = default;

    [[nodiscard]]
    std::string_view format(std::span<char> buf) const noexcept;

    void reset() noexcept {
        family = Family::none;
        port = 0;
        addr = {};
    }
};

} // namespace ddcs::ctrl::infra::transport
