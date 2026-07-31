#include "ddcs/wire/frame/frame.hpp"

#include "ddcs/common/endian.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

namespace ddcs::wire::frame {

namespace {

constexpr std::size_t magic_offset = 0;
constexpr std::size_t payload_length_offset = 2;

// frame 헤더 codec. wire 표면에는 encode_frame/decode_frame만 노출한다.
using HeaderBytes = std::array<std::byte, header_size>;

[[nodiscard]] HeaderBytes encode_header(std::uint16_t payload_length) noexcept {
    HeaderBytes bytes{};
    auto const encoded_magic = common::to_be(magic_value);
    auto const encoded_length = common::to_be(payload_length);
    std::memcpy(bytes.data() + magic_offset, &encoded_magic, sizeof(encoded_magic));
    std::memcpy(bytes.data() + payload_length_offset, &encoded_length, sizeof(encoded_length));
    return bytes;
}

// 헤더를 검증하고 payload_length를 반환한다. magic 불일치면 nullopt
[[nodiscard]] std::optional<std::uint16_t> decode_header(HeaderBytes const& bytes) noexcept {
    std::uint16_t magic_raw{};
    std::uint16_t length_raw{};
    std::memcpy(&magic_raw, bytes.data() + magic_offset, sizeof(magic_raw));
    std::memcpy(&length_raw, bytes.data() + payload_length_offset, sizeof(length_raw));

    if (common::from_be(magic_raw) != magic_value) {
        return std::nullopt;
    }
    return common::from_be(length_raw);
}

} // namespace

bool encode_frame(common::LinearBuffer& message) noexcept {
    if (message.size() > max_payload_length) {
        return false;
    }
    auto const header = encode_header(static_cast<std::uint16_t>(message.size()));
    return message.prepend(header);
}

DecodeResult decode_frame(
    common::CircularBuffer& rx, common::ObjectPool<common::LinearBuffer>& pool,
    common::PoolHandle<common::LinearBuffer>& out
) {
    if (rx.size() < header_size) {
        return DecodeResult::incomplete;
    }

    HeaderBytes hb{};
    (void)rx.peek({hb.data(), hb.size()}); // header_size 충족 확인 후라 항상 성공
    auto const parsed_length = decode_header(hb);
    if (!parsed_length) {
        return DecodeResult::bad_magic;
    }

    std::uint16_t const payload_length = *parsed_length;
    if (payload_length > max_payload_length) {
        return DecodeResult::too_long;
    }

    std::size_t const total = header_size + payload_length;
    if (rx.size() < total) {
        return DecodeResult::incomplete; // 부분 frame
    }

    // ring을 변형하기 전에 pool 버퍼를 먼저 확보한다.
    // acquire는 풀 증설 시 bad_alloc을 던질 수 있는 유일한 연산이라
    // consume 앞에 두면 throw해도 ring이 desync되지 않는다.
    auto payload = pool.acquire();
    if (!rx.consume(header_size)) {
        return DecodeResult::read_error;
    }
    if (payload_length > 0) {
        auto const w = payload->tailroom_span();
        if (!rx.read(w.first(payload_length)) || !payload->commit(payload_length)) {
            return DecodeResult::read_error;
        }
    }

    out = std::move(payload);
    return DecodeResult::ok;
}

} // namespace ddcs::wire::frame
