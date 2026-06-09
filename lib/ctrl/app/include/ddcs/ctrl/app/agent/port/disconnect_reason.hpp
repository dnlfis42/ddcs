#pragma once

#include <cstdint>

namespace ddcs::ctrl::app::agent::port {

enum class DisconnectReason : std::uint8_t {
    local_drop,  // 우리가 닫음
    peer_closed, // peer가 FIN 보냄
    io_error,    // kernel/network I/O 오류
    dacp_error,  // DACP protocol 오류
};

} // namespace ddcs::ctrl::app::agent::port
