#pragma once

#include <cstdint>
#include <optional>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace ddcs::net {

// 바인드된 소켓의 로컬 포트(호스트 바이트 순서). getsockname 실패면 nullopt(errno 유지)
[[nodiscard]] inline std::optional<std::uint16_t> bound_port(int fd) noexcept {
    sockaddr_in addr{};
    socklen_t len{sizeof(addr)};

    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return std::nullopt;
    }
    return ::ntohs(addr.sin_port);
}

} // namespace ddcs::net
