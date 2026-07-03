#pragma once

#include "ddcs/common/linear_buffer.hpp"

namespace ddcs::wire::frame {

// 송신 프레이밍. agent/ctrl transport가 공유한다(rx 대칭은 extract.hpp):
// reserve_header_room으로 headroom을 예약하고, payload를 쓴 뒤 seal이 길이 헤더를 제자리
// prepend한다(복사 없는 조립 경로).

// 빈 송신 버퍼에 frame 헤더 자리를 예약한다. 용량 부족이면 false.
[[nodiscard]] bool reserve_header_room(common::LinearBuffer& message) noexcept;

// payload 상한을 검사하고 길이 헤더를 headroom에 제자리 prepend한다.
// payload가 max_payload_length 초과이거나 reserve_header_room을 거치지 않은 buffer면 false.
[[nodiscard]] bool seal(common::LinearBuffer& message) noexcept;

} // namespace ddcs::wire::frame
