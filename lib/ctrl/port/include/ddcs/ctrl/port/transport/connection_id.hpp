#pragma once

#include "ddcs/common/strong_value.hpp"

#include <cstdint>

namespace ddcs::ctrl::port::transport {

// 전송 연결의 식별자. infra(transport)가 accept 시 mint.
// app/infra 양 끝이 공유하는 상관 토큰(계약). 기본값 ConnectionId{} = 무효.
using ConnectionId = common::StrongValue<struct ConnectionIdTag, std::uint64_t>;

} // namespace ddcs::ctrl::port::transport
