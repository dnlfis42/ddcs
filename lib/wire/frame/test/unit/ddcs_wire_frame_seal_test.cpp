#include "ddcs/wire/frame/seal.hpp"

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/wire/frame/extract.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace frame = ddcs::wire::frame;

using ddcs::common::CircularBuffer;
using ddcs::common::LinearBuffer;
using ddcs::common::ObjectPool;
using ddcs::common::PoolHandle;

std::span<std::byte const> as_bytes(std::string_view s) {
    return {reinterpret_cast<std::byte const*>(s.data()), s.size()};
}

TEST(FrameSealTest, ReserveThenSealPrependsLengthHeader) {
    LinearBuffer message{64};
    ASSERT_TRUE(frame::reserve_header_room(message));
    ASSERT_TRUE(message.try_append(as_bytes("abc")));

    ASSERT_TRUE(frame::seal(message));

    auto const bytes = message.data_span();
    ASSERT_EQ(bytes.size(), frame::header_size + 3);
    frame::HeaderBytes header{};
    std::copy_n(bytes.begin(), frame::header_size, header.begin());
    auto const length = frame::decode(header);
    ASSERT_TRUE(length.has_value());
    EXPECT_EQ(*length, 3u);
}

TEST(FrameSealTest, SealFailsWithoutHeaderRoom) {
    LinearBuffer message{64};
    ASSERT_TRUE(message.try_append(as_bytes("abc"))); // headroom 예약 없음

    EXPECT_FALSE(frame::seal(message));
}

TEST(FrameSealTest, SealFailsOverMaxPayload) {
    LinearBuffer message{frame::header_size + frame::max_payload_length + 8};
    ASSERT_TRUE(frame::reserve_header_room(message));
    std::vector<std::byte> const big(frame::max_payload_length + 1);
    ASSERT_TRUE(message.try_append(big));

    EXPECT_FALSE(frame::seal(message));
}

// 송신(seal)과 수신(pull_frame)이 같은 frame 규약을 공유하는지 왕복으로 확인한다.
TEST(FrameSealTest, SealedFrameRoundTripsThroughExtract) {
    LinearBuffer message{64};
    ASSERT_TRUE(frame::reserve_header_room(message));
    ASSERT_TRUE(message.try_append(as_bytes("abc")));
    ASSERT_TRUE(frame::seal(message));

    CircularBuffer rx{1024};
    ASSERT_TRUE(rx.try_write(message.data_span()));
    auto pool = ObjectPool<LinearBuffer>::create<4>(std::size_t{64});
    PoolHandle<LinearBuffer> out;

    ASSERT_EQ(frame::pull_frame(rx, pool, out), frame::PullResult::ok);
    auto const payload = out->data_span();
    ASSERT_EQ(payload.size(), 3u);
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), as_bytes("abc").begin()));
}

} // namespace
