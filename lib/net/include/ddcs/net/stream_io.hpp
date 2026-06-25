#pragma once

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <queue>

#include <sys/socket.h>
#include <sys/types.h>

namespace ddcs::net {

// 비차단(edge-triggered) 소켓 스트림 I/O 결과. agent/ctrl transport Connection이 공유한다.
// 상태 전이는 하지 않고 syscall 결과만 보고한다. (전이는 호출부 책임)
enum class StreamResult : std::uint8_t {
    ok,          // transmit: tx 큐를 모두 비움
    full,        // receive: rx 버퍼가 가득 참 (framing으로 비운 뒤 재시도)
    would_block, // EAGAIN/EWOULDBLOCK: 더 진행 불가, 정상
    peer_closed, // receive: FIN (recv가 0)
    error,       // 복구 불가
};

// ET: 더 읽을 게 없을 때(EAGAIN)까지 fd를 rx 버퍼로 소진한다.
[[nodiscard]] inline StreamResult receive_into(int fd, common::CircularBuffer& rx) noexcept {
    for (;;) {
        auto dst = rx.writable_span();
        if (dst.empty()) {
            return StreamResult::full;
        }

        ssize_t n;
        do {
            n = ::recv(fd, dst.data(), dst.size(), 0);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            if (!rx.try_commit(static_cast<std::size_t>(n))) {
                return StreamResult::error;
            }
            continue;
        }
        if (n == 0) {
            return StreamResult::peer_closed; // FIN
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return StreamResult::would_block;
        }
        return StreamResult::error;
    }
}

// ET: 커널 송신 버퍼가 막힐 때(EAGAIN)까지 tx 큐를 비운다.
[[nodiscard]] inline StreamResult
transmit_from(int fd, std::queue<common::PoolHandle<common::LinearBuffer>>& tx) noexcept {
    while (!tx.empty()) {
        auto& buffer = *tx.front();
        auto data = buffer.data_span();
        if (data.empty()) {
            tx.pop();
            continue;
        }

        ssize_t n;
        do {
            n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            if (!buffer.try_consume(static_cast<std::size_t>(n))) {
                return StreamResult::error;
            }
            if (buffer.size() == 0) {
                tx.pop();
            }
            continue;
        }
        if (n == 0) {
            // send()가 0을 반환하는 경우는 사실상 없지만 errno가 설정되지 않으므로
            // 아래의 stale errno 판독을 막기 위해 명시적으로 처리한다.
            return StreamResult::error;
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return StreamResult::would_block; // 커널 버퍼 포화 시 writable 무장
        }
        return StreamResult::error;
    }
    return StreamResult::ok;
}

} // namespace ddcs::net
