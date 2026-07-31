#pragma once

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace ddcs::wire::frame {

inline constexpr std::size_t header_size = 4;
inline constexpr std::uint16_t magic_value = 0xDDC5;
inline constexpr std::size_t max_payload_length = 1024;
inline constexpr std::size_t max_frame_size = header_size + max_payload_length;

// rx 링 용량 상한. 연결마다 하나씩 잡히므로 설정 오타가 곧 대형 할당이 된다.
// 2의 거듭제곱이라 fit_rx_capacity(max_rx_capacity)는 자기 자신이다.
inline constexpr std::size_t max_rx_capacity = std::size_t{1} << 20;

[[nodiscard]] constexpr std::size_t fit_rx_capacity(std::size_t requested) noexcept {
    return std::bit_ceil(requested < max_frame_size ? max_frame_size : requested);
}

// 송신 프레이밍. payload 상한을 검사하고 길이 헤더를 headroom에 제자리 prepend한다.
// payload가 max_payload_length 초과이거나 headroom에 header_size를 확보하지 않은 buffer면 false.
[[nodiscard]] bool encode_frame(common::LinearBuffer& message) noexcept;

// rx ring에서 완성 frame 하나를 디코딩한 결과
enum class DecodeResult : std::uint8_t {
    ok,         // 완성 frame 1개를 out으로 디코딩 (`[type][body]`)
    incomplete, // header 미달 또는 부분 frame: 더 기다림(추출 없음)
    bad_magic,  // magic 불일치: protocol error
    too_long,   // payload_length > max_payload_length: protocol error
    read_error, // rx_consume/rx_read/commit 실패: 손상
};

// 로그/진단용 이름. 어휘 밖 값은 빈 문자열로 노출한다.
constexpr std::string_view to_string(DecodeResult result) noexcept {
    switch (result) {
    case DecodeResult::ok:
        return "ok";
    case DecodeResult::incomplete:
        return "incomplete";
    case DecodeResult::bad_magic:
        return "bad_magic";
    case DecodeResult::too_long:
        return "too_long";
    case DecodeResult::read_error:
        return "read_error";
    }
    return {};
}

// rx ring에서 완성 frame 하나를 pool에서 받은 버퍼(out)로 디코딩한다.
// ok면 out은 msg payload(`[type][body]`)를 통째로 담는다. incomplete/error면 out은 건드리지 않는다.
[[nodiscard]] DecodeResult decode_frame(
    common::CircularBuffer& rx, common::ObjectPool<common::LinearBuffer>& pool,
    common::PoolHandle<common::LinearBuffer>& out
);

/// @brief rx 버퍼 안의 유효한 frame들을 전부 추출해서 처리한다.
///
/// @tparam GetRx CircularBuffer*()
/// @tparam OnFrame void(PoolHandle<LinearBuffer>)
/// @tparam OnError void(DecodeResult)
/// @param pool 버퍼 풀
/// @param get_rx
/// @param on_frame 디코딩한 payload 1개를 처리하는 콜백
/// @param on_error
template <typename GetRx, typename OnFrame, typename OnError>
void dispatch_frames(
    common::ObjectPool<common::LinearBuffer>& pool, GetRx&& get_rx, OnFrame&& on_frame,
    OnError&& on_error
) {
    for (;;) {
        common::CircularBuffer* rx = get_rx();
        if (rx == nullptr) {
            return;
        }

        common::PoolHandle<common::LinearBuffer> payload;
        DecodeResult const result = decode_frame(*rx, pool, payload);
        switch (result) {
        case DecodeResult::ok:
            on_frame(std::move(payload));
            break; // 다음 반복에서 get_rx로 연결 생존 재확인
        case DecodeResult::incomplete:
            return;
        case DecodeResult::bad_magic:
        case DecodeResult::too_long:
        case DecodeResult::read_error:
            on_error(result);
            return;
        }
    }
}

} // namespace ddcs::wire::frame
