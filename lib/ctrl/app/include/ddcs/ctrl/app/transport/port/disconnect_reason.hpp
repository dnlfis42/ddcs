#pragma once

#include <cstdint>

namespace ddcs::ctrl::app::transport::port {

enum class DisconnectReason : std::uint8_t {
    local_drop,     // 우리가 닫음
    peer_closed,    // peer가 FIN 보냄
    io_error,       // kernel/network I/O 오류
    protocol_error, // wire protocol 오류
};

} // namespace ddcs::ctrl::app::transport::port
