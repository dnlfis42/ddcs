#pragma once

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <cstdint>
#include <utility>

namespace ddcs::wire::frame {

// rx ring에서 완성 frame 하나를 추출한 결과
enum class PullResult : std::uint8_t {
    incomplete, // header 미달 또는 부분 frame: 더 기다림(추출 없음)
    bad_magic,  // magic 불일치: protocol error
    too_long,   // payload_length > max_payload_length: protocol error
    read_error, // rx_consume/rx_read/commit 실패: 손상
    ok,         // 완성 frame 1개를 out으로 추출 (`[type][body]`)
};

// rx ring에서 완성 frame 하나를 pool에서 받은 버퍼(out)로 추출한다.
// ok면 out은 msg payload(`[type][body]`)를 통째를 담는다. incomplete/error면 out은 건드리지 않는다.
[[nodiscard]] PullResult pull_frame(
    common::CircularBuffer& rx, common::ObjectPool<common::LinearBuffer>& pool,
    common::PoolHandle<common::LinearBuffer>& out
);

// rx ring의 완성 frame을 모두 추출해 콜백으로 올린다. agent/ctrl transport가 공유하는 단일 루프.
//
// get_rx:
// 매 반복 호출. 현재 연결의 rx ring 포인터(없으면 nullptr -> 종료).
// on_frame 콜백 안에서 연결이 사라지는 재진입을, 반복 재조회(state 재확인/find_active)로 흡수한다.
// on_frame: 완성 payload 1개를 위로 올린다. void(PoolHandle<LinearBuffer>).
// on_error:
// protocol error(bad_magic/too_long/read_error) 시 1회 호출하고 루프 종료.
// 끊는 정책은 호출부 책임. void(PullResult).
template <typename GetRx, typename OnFrame, typename OnError>
void extract_frames(
    common::ObjectPool<common::LinearBuffer>& pool, GetRx&& get_rx, OnFrame&& on_frame,
    OnError&& on_error
) {
    for (;;) {
        common::CircularBuffer* rx = get_rx();
        if (rx == nullptr) {
            return;
        }

        common::PoolHandle<common::LinearBuffer> payload;
        PullResult const result = pull_frame(*rx, pool, payload);
        switch (result) {
        case PullResult::incomplete:
            return;
        case PullResult::bad_magic:
        case PullResult::too_long:
        case PullResult::read_error:
            on_error(result);
            return;
        case PullResult::ok:
            on_frame(std::move(payload));
            break; // 다음 반복에서 get_rx로 연결 생존 재확인
        }
    }
}

} // namespace ddcs::wire::frame
