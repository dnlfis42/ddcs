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

// 비차단(edge-triggered) 소켓 수신 결과
//
// 방향마다 낼 수 있는 결과가 달라 타입을 나눠 둔다. 한 enum이 양방향을 겸하면 호출부가
// 도달 불가한 case를 적게 되고, 그 분기를 지키는 런타임 검사까지 딸려 온다.
struct ReceiveResult {
    enum class Code : std::uint8_t {
        would_block, // EAGAIN/EWOULDBLOCK: 더 읽을 것 없음, 정상 (커널 backpressure)
        full,        // rx 버퍼가 가득 참 (앱 backpressure)
        peer_closed, // FIN (recv가 0)
        error,       // 복구 불가
    };

    Code code;
    int err = 0; // code == error인 syscall 실패의 errno. 0이면 내부 오류
};

// 비차단(edge-triggered) 소켓 송신 결과
struct TransmitResult {
    enum class Code : std::uint8_t {
        drained,     // tx 큐를 모두 비움
        would_block, // EAGAIN/EWOULDBLOCK: 커널 송신 버퍼 포화
        error,       // 복구 불가
    };

    Code code;
    int err = 0; // code == error인 syscall 실패의 errno. 0이면 내부 오류
};

// ET: 더 읽을 게 없을 때(EAGAIN)까지 fd를 buf로 소진한다.
//
// 상태 전이는 하지 않고 syscall 결과만 보고한다.
[[nodiscard]] inline ReceiveResult receive_into(int fd, common::CircularBuffer& buf) noexcept {
    for (;;) {
        auto dst = buf.writable_span();
        if (dst.empty()) {
            return {.code = ReceiveResult::Code::full};
        }

        ssize_t n;
        do {
            n = ::recv(fd, dst.data(), dst.size(), 0);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            if (!buf.commit(static_cast<std::size_t>(n))) {
                return {.code = ReceiveResult::Code::error};
            }
            continue;
        }
        if (n == 0) {
            return {.code = ReceiveResult::Code::peer_closed}; // FIN
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return {.code = ReceiveResult::Code::would_block};
        }
        return {.code = ReceiveResult::Code::error, .err = err};
    }
}

// ET: 커널 송신 버퍼가 막힐 때(EAGAIN)까지 que를 비운다.
//
// 상태 전이는 하지 않고 syscall 결과만 보고한다.
[[nodiscard]] inline TransmitResult
transmit_from(int fd, std::queue<common::PoolHandle<common::LinearBuffer>>& que) noexcept {
    while (!que.empty()) {
        auto& buffer = *que.front();
        auto data = buffer.data_span();
        if (data.empty()) {
            que.pop();
            continue;
        }

        ssize_t n;
        do {
            n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
        } while (n < 0 && errno == EINTR);

        if (n > 0) {
            if (!buffer.consume(static_cast<std::size_t>(n))) {
                return {.code = TransmitResult::Code::error};
            }
            if (buffer.size() == 0) {
                que.pop();
            }
            continue;
        }
        if (n == 0) {
            // send()가 0을 반환하는 경우는 사실상 없지만 errno도 설정하지 않으므로
            // 아래의 stale errno 판독을 막기 위해 명시적으로 처리한다.
            return {.code = TransmitResult::Code::error};
        }

        int const err = errno;
        if (err == EAGAIN || err == EWOULDBLOCK) {
            return {.code = TransmitResult::Code::would_block}; // 커널 버퍼 포화 시 writable 대기
        }
        return {.code = TransmitResult::Code::error, .err = err};
    }
    return {.code = TransmitResult::Code::drained};
}

} // namespace ddcs::net
