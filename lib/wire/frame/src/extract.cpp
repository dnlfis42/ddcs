#include "ddcs/wire/frame/extract.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace ddcs::wire::frame {

PullResult pull_frame(
    common::CircularBuffer& rx, common::ObjectPool<common::LinearBuffer>& pool,
    common::PoolHandle<common::LinearBuffer>& out
) {
    if (rx.size() < header_size) {
        return PullResult::incomplete;
    }

    HeaderBytes hb{};
    (void)rx.try_peek({hb.data(), hb.size()}); // header_size 충족 확인 후라 항상 성공
    auto const parsed_length = decode(hb);
    if (!parsed_length) {
        return PullResult::bad_magic;
    }

    std::uint16_t const payload_length = *parsed_length;
    if (payload_length > max_payload_length) {
        return PullResult::too_long;
    }

    std::size_t const total = header_size + payload_length;
    if (rx.size() < total) {
        return PullResult::incomplete; // 부분 frame
    }

    // ring을 변형하기 전에 pool 버퍼를 먼저 확보한다.
    // acquire는 풀 증설 시 bad_alloc을 던질 수 있는 유일한 연산이라
    // consume 앞에 두면 throw해도 ring이 desync되지 않는다.
    auto payload = pool.acquire();
    if (!rx.try_consume(header_size)) {
        return PullResult::read_error;
    }
    if (payload_length > 0) {
        auto const w = payload->tailroom_span();
        if (!rx.try_read(w.first(payload_length)) || !payload->try_commit(payload_length)) {
            return PullResult::read_error;
        }
    }

    out = std::move(payload);
    return PullResult::ok;
}

} // namespace ddcs::wire::frame
