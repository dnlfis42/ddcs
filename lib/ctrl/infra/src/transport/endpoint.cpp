#include "ddcs/ctrl/infra/transport/endpoint.hpp"

#include <span>
#include <string_view>

#include <cstddef>
#include <cstdio>

#include <arpa/inet.h>
#include <netinet/in.h>

namespace ddcs::ctrl::infra::transport {

namespace {

// [ + INET6_ADDRSTRLEN(널 종단 포함) + ] + : + 포트(최대 5)
constexpr std::size_t endpoint_format_max_len = 1 + INET6_ADDRSTRLEN + 1 + 1 + 5;
static_assert(
    endpoint_format_min_size >= endpoint_format_max_len, "endpoint_format_min_size must accommodate worst-case format"
);

} // namespace

std::string_view Endpoint::format(std::span<char> buf) const noexcept {
    if (family == Family::none || buf.size() < endpoint_format_min_size) {
        return {};
    }

    int const af = (family == Family::v4) ? AF_INET : AF_INET6;

    char ip[INET6_ADDRSTRLEN]{};
    if (::inet_ntop(af, addr.data(), ip, sizeof(ip)) == nullptr) {
        return {};
    }

    int const written = (family == Family::v4) ? std::snprintf(buf.data(), buf.size(), "%s:%u", ip, port)
                                               : std::snprintf(buf.data(), buf.size(), "[%s]:%u", ip, port);
    if (written < 0) {
        return {};
    }

    return std::string_view{buf.data(), static_cast<std::size_t>(written)};
}

} // namespace ddcs::ctrl::infra::transport
