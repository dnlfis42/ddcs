#include "ddcs/wire/frame/frame.hpp"

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace frame = ddcs::wire::frame;

using ddcs::common::CircularBuffer;
using ddcs::common::LinearBuffer;
using ddcs::common::ObjectPool;
using ddcs::common::PoolHandle;
using frame::decode_frame;
using frame::DecodeResult;

ObjectPool<LinearBuffer> make_pool() {
    return ObjectPool<LinearBuffer>::create<8>(std::size_t{frame::max_frame_size});
}

std::span<std::byte const> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

void write_bytes(CircularBuffer& rx, std::span<std::byte const> bytes) {
    ASSERT_TRUE(rx.write(bytes));
}

// frame 헤더를 wire 표면을 거치지 않고 peer 관점의 리터럴 바이트로 조립한다.
// (magic `0xDDC5` + length, big-endian)
std::array<std::byte, frame::header_size> make_header(std::uint16_t payload_length) {
    return {
        std::byte{0xDD}, std::byte{0xC5}, static_cast<std::byte>(payload_length >> 8),
        static_cast<std::byte>(payload_length & 0xFF)
    };
}

// rx ring에 완성 frame(header + `[type][body]`) 하나를 써넣는다.
void push_frame(CircularBuffer& rx, std::uint8_t type, std::string_view body) {
    std::string payload;
    payload.push_back(static_cast<char>(type));
    payload.append(body);
    auto const hb = make_header(static_cast<std::uint16_t>(payload.size()));
    write_bytes(rx, hb);
    write_bytes(rx, as_bytes(payload));
}

// rx 링은 CircularBuffer 계약(2의 거듭제곱)과 frame 하한을 동시에 만족해야 하는데,
// 두 계약이 만나는 곳이 이 함수뿐이라 어떤 요청값이 와도 결과가 둘 다 지켜야 한다.
static_assert(frame::fit_rx_capacity(1) >= frame::max_frame_size);
static_assert(std::has_single_bit(frame::fit_rx_capacity(frame::max_frame_size)));
static_assert(std::has_single_bit(frame::fit_rx_capacity(5000)));

TEST(WireFrameTest, FitRxCapacitySatisfiesRingContract) {
    for (std::size_t const requested :
         {std::size_t{1}, std::size_t{512}, frame::max_frame_size, std::size_t{1500},
          std::size_t{2048}, std::size_t{4096}, std::size_t{5000}}) {
        std::size_t const fitted = frame::fit_rx_capacity(requested);

        EXPECT_GE(fitted, frame::max_frame_size) << "requested=" << requested;
        EXPECT_TRUE(std::has_single_bit(fitted)) << "requested=" << requested;
        EXPECT_NO_THROW(CircularBuffer{fitted}) << "requested=" << requested;
    }
}

TEST(WireFrameTest, FitRxCapacityKeepsAlreadyValidRequest) {
    EXPECT_EQ(frame::fit_rx_capacity(2048), 2048u);
    EXPECT_EQ(frame::fit_rx_capacity(4096), 4096u);

    EXPECT_EQ(frame::fit_rx_capacity(512), 2048u);  // 하한 미달은 하한 이상으로 올린다
    EXPECT_EQ(frame::fit_rx_capacity(5000), 8192u); // 하한을 넘어도 2의 거듭제곱으로 올린다
}

TEST(WireFrameTest, EncodePrependsBigEndianHeader) {
    LinearBuffer message{64};
    ASSERT_TRUE(message.set_headroom(frame::header_size));
    ASSERT_TRUE(message.append(as_bytes("abc")));

    ASSERT_TRUE(frame::encode_frame(message));

    auto const bytes = message.data_span();
    ASSERT_EQ(bytes.size(), frame::header_size + 3);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0xDDu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[1]), 0xC5u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[2]), 0x00u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x03u);
}

TEST(WireFrameTest, EncodeWritesLengthAsBigEndian) {
    LinearBuffer message{frame::header_size + frame::max_payload_length};
    ASSERT_TRUE(message.set_headroom(frame::header_size));
    std::vector<std::byte> const body(0x0304); // length 상하위 바이트가 모두 0이 아닌 값
    ASSERT_TRUE(message.append(body));

    ASSERT_TRUE(frame::encode_frame(message));

    auto const bytes = message.data_span();
    EXPECT_EQ(std::to_integer<unsigned>(bytes[2]), 0x03u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x04u);
}

TEST(WireFrameTest, EncodeFailsWithoutHeaderRoom) {
    LinearBuffer message{64};
    ASSERT_TRUE(message.append(as_bytes("abc"))); // headroom 확보 없음

    EXPECT_FALSE(frame::encode_frame(message));
}

TEST(WireFrameTest, EncodeFailsOverMaxPayload) {
    LinearBuffer message{frame::header_size + frame::max_payload_length + 8};
    ASSERT_TRUE(message.set_headroom(frame::header_size));
    std::vector<std::byte> const big(frame::max_payload_length + 1);
    ASSERT_TRUE(message.append(big));

    EXPECT_FALSE(frame::encode_frame(message));
}

TEST(WireFrameTest, IncompleteWhenBelowHeader) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    std::array<std::byte, 2> const partial{}; // header_size=4, 2바이트만
    write_bytes(rx, {partial.data(), partial.size()});

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(decode_frame(rx, pool, out), DecodeResult::incomplete);
    EXPECT_EQ(rx.size(), 2u); // 소비 안 함
}

TEST(WireFrameTest, IncompleteOnPartialBody) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    auto const hb = make_header(4); // payload 4바이트 선언
    write_bytes(rx, hb);
    std::array<std::byte, 2> const body{}; // body 4 중 2만 도착
    write_bytes(rx, {body.data(), body.size()});

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(decode_frame(rx, pool, out), DecodeResult::incomplete);
    EXPECT_EQ(rx.size(), frame::header_size + 2); // 부분 frame은 보류(소비 안 함)
}

TEST(WireFrameTest, BadMagicDetected) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    std::array<std::byte, 4> const junk{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0x00}, std::byte{0x00}
    };
    write_bytes(rx, {junk.data(), junk.size()});

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(decode_frame(rx, pool, out), DecodeResult::bad_magic);
}

TEST(WireFrameTest, TooLongDetected) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    auto const hb = make_header(static_cast<std::uint16_t>(frame::max_payload_length + 1));
    write_bytes(rx, hb); // header만으로 판정(too_long은 길이만 본다)

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(decode_frame(rx, pool, out), DecodeResult::too_long);
}

TEST(WireFrameTest, GoodFrameDecodedWholePayload) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    push_frame(rx, 0x42, "abc");

    PoolHandle<LinearBuffer> out;
    ASSERT_EQ(decode_frame(rx, pool, out), DecodeResult::ok);
    ASSERT_TRUE(static_cast<bool>(out));
    auto const d = out->data_span();
    ASSERT_EQ(d.size(), 4u); // `[type][body]`
    EXPECT_EQ(static_cast<std::uint8_t>(d[0]), 0x42u);
    EXPECT_EQ(std::memcmp(d.data() + 1, "abc", 3), 0);
    EXPECT_EQ(rx.size(), 0u); // 완전히 소비함
}

TEST(WireFrameTest, ZeroLengthFrameDecoded) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    auto const hb = make_header(0); // payload 0바이트
    write_bytes(rx, hb);

    PoolHandle<LinearBuffer> out;
    ASSERT_EQ(decode_frame(rx, pool, out), DecodeResult::ok);
    ASSERT_TRUE(static_cast<bool>(out));
    EXPECT_EQ(out->data_span().size(), 0u);
    EXPECT_EQ(rx.size(), 0u);
}

TEST(WireFrameTest, MultipleFramesDecodedSequentially) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    push_frame(rx, 0x01, "one");
    push_frame(rx, 0x02, "two");

    PoolHandle<LinearBuffer> a;
    PoolHandle<LinearBuffer> b;
    PoolHandle<LinearBuffer> c;
    ASSERT_EQ(decode_frame(rx, pool, a), DecodeResult::ok);
    ASSERT_EQ(decode_frame(rx, pool, b), DecodeResult::ok);
    EXPECT_EQ(decode_frame(rx, pool, c), DecodeResult::incomplete); // 둘 다 소비 후 빈 ring

    EXPECT_EQ(static_cast<std::uint8_t>(a->data_span()[0]), 0x01u);
    EXPECT_EQ(static_cast<std::uint8_t>(b->data_span()[0]), 0x02u);
}

// 송신(encode_frame)과 수신(decode_frame)이 같은 frame 규약을 공유하는지 왕복으로 확인한다.
TEST(WireFrameTest, EncodedFrameRoundTripsThroughDecode) {
    LinearBuffer message{64};
    ASSERT_TRUE(message.set_headroom(frame::header_size));
    ASSERT_TRUE(message.append(as_bytes("abc")));
    ASSERT_TRUE(frame::encode_frame(message));

    CircularBuffer rx{1024};
    ASSERT_TRUE(rx.write(message.data_span()));
    auto pool = ObjectPool<LinearBuffer>::create<4>(std::size_t{64});
    PoolHandle<LinearBuffer> out;

    ASSERT_EQ(decode_frame(rx, pool, out), DecodeResult::ok);
    auto const payload = out->data_span();
    ASSERT_EQ(payload.size(), 3u);
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), as_bytes("abc").begin()));
}

TEST(WireFrameTest, DispatchDeliversAllCompleteFrames) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    push_frame(rx, 0x01, "one");
    push_frame(rx, 0x02, "two");
    std::array<std::byte, 2> const partial{}; // 뒤따르는 부분 frame
    write_bytes(rx, {partial.data(), partial.size()});

    std::vector<std::uint8_t> types;
    frame::dispatch_frames(
        pool, [&]() { return &rx; },
        [&](PoolHandle<LinearBuffer> payload) {
            types.push_back(static_cast<std::uint8_t>(payload->data_span()[0]));
        },
        [](DecodeResult) { FAIL() << "unexpected on_error"; }
    );

    EXPECT_EQ(types, (std::vector<std::uint8_t>{0x01, 0x02}));
    EXPECT_EQ(rx.size(), 2u); // 부분 frame은 다음 수신까지 보류
}

TEST(WireFrameTest, DispatchStopsAfterErrorOnce) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    std::array<std::byte, 4> const junk{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0x00}, std::byte{0x00}
    };
    write_bytes(rx, {junk.data(), junk.size()});
    push_frame(rx, 0x01, "one"); // 오류 뒤에 이미 도착해 있는 frame

    int frames = 0;
    std::vector<DecodeResult> errors;
    frame::dispatch_frames(
        pool, [&]() { return &rx; }, [&](PoolHandle<LinearBuffer>) { ++frames; },
        [&](DecodeResult reason) { errors.push_back(reason); }
    );

    EXPECT_EQ(frames, 0);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], DecodeResult::bad_magic);
}

// on_frame 처리 중 연결이 사라지는 재진입: get_rx가 nullptr을 주면 루프가 끝나고
// ring에 남은 frame은 배달하지 않는다.
TEST(WireFrameTest, DispatchStopsWhenGetRxTurnsNull) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    push_frame(rx, 0x01, "one");
    push_frame(rx, 0x02, "two");

    int frames = 0;
    bool alive = true;
    frame::dispatch_frames(
        pool, [&]() -> CircularBuffer* { return alive ? &rx : nullptr; },
        [&](PoolHandle<LinearBuffer>) {
            ++frames;
            alive = false; // 콜백이 연결을 닫는 시나리오
        },
        [](DecodeResult) { FAIL() << "unexpected on_error"; }
    );

    EXPECT_EQ(frames, 1); // 두 번째 frame은 배달하지 않는다
}

} // namespace
