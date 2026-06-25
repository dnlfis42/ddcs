#include "ddcs/ctrl/infra/transport/peer_address.hpp"

#include <cstddef>
#include <cstdio>
#include <span>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>

namespace ddcs::ctrl::infra::transport {

namespace {

// INET6_ADDRSTRLEN은 nul까지 포함한다. bracket 표기와 최대 5자리 port를 더한 worst-case 길이
constexpr std::size_t required_peer_address_format_buffer_size = 1 + INET6_ADDRSTRLEN + 1 + 1 + 5;

static_assert(
    min_peer_address_format_buffer_size >= required_peer_address_format_buffer_size,
    "min_peer_address_format_buffer_size must accommodate worst-case format"
);

} // namespace

std::string_view PeerAddress::format(std::span<char> buf) const noexcept {
    if (family == Family::none || buf.size() < min_peer_address_format_buffer_size) {
        return {};
    }

    int const af = (family == Family::v4) ? AF_INET : AF_INET6;

    char ip[INET6_ADDRSTRLEN]{};
    if (::inet_ntop(af, addr.data(), ip, sizeof(ip)) == nullptr) {
        return {};
    }

    int const written = (family == Family::v4)
                            ? std::snprintf(buf.data(), buf.size(), "%s:%u", ip, port)
                            : std::snprintf(buf.data(), buf.size(), "[%s]:%u", ip, port);
    if (written < 0) {
        return {};
    }

    return std::string_view{buf.data(), static_cast<std::size_t>(written)};
}

} // namespace ddcs::ctrl::infra::transport
