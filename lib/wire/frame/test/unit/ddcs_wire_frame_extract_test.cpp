#include "ddcs/wire/frame/extract.hpp"

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace {

namespace frame = ddcs::wire::frame;

using ddcs::common::CircularBuffer;
using ddcs::common::LinearBuffer;
using ddcs::common::ObjectPool;
using ddcs::common::PoolHandle;
using frame::pull_frame;
using frame::PullResult;

ObjectPool<LinearBuffer> make_pool() {
    return ObjectPool<LinearBuffer>::create<8>(
        std::size_t{frame::header_size + frame::max_payload_length}
    );
}

void write_bytes(CircularBuffer& rx, std::span<std::byte const> bytes) {
    ASSERT_TRUE(rx.try_write(bytes));
}

// rx ring에 완성 frame(header + `[type][body]`) 하나를 써넣는다.
void push_frame(CircularBuffer& rx, std::uint8_t type, std::string_view body) {
    std::string payload;
    payload.push_back(static_cast<char>(type));
    payload.append(body);
    auto const hb = frame::encode(static_cast<std::uint16_t>(payload.size()));
    write_bytes(rx, {hb.data(), hb.size()});
    write_bytes(rx, {reinterpret_cast<std::byte const*>(payload.data()), payload.size()});
}

TEST(WireFrameExtractTest, IncompleteWhenBelowHeader) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    std::array<std::byte, 2> const partial{}; // header_size=4, 2바이트만
    write_bytes(rx, {partial.data(), partial.size()});

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(pull_frame(rx, pool, out), PullResult::incomplete);
    EXPECT_EQ(rx.size(), 2u); // 소비 안 함
}

TEST(WireFrameExtractTest, IncompleteOnPartialBody) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    auto const hb = frame::encode(4); // payload 4바이트 선언
    write_bytes(rx, {hb.data(), hb.size()});
    std::array<std::byte, 2> const body{}; // body 4 중 2만 도착
    write_bytes(rx, {body.data(), body.size()});

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(pull_frame(rx, pool, out), PullResult::incomplete);
    EXPECT_EQ(rx.size(), frame::header_size + 2); // 부분 frame은 보류(소비 안 함)
}

TEST(WireFrameExtractTest, BadMagicDetected) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    std::array<std::byte, 4> const junk{
        std::byte{0xDE}, std::byte{0xAD}, std::byte{0x00}, std::byte{0x00}
    };
    write_bytes(rx, {junk.data(), junk.size()});

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(pull_frame(rx, pool, out), PullResult::bad_magic);
}

TEST(WireFrameExtractTest, TooLongDetected) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    auto const hb = frame::encode(static_cast<std::uint16_t>(frame::max_payload_length + 1));
    write_bytes(rx, {hb.data(), hb.size()}); // header만으로 판정(too_long은 길이만 본다)

    PoolHandle<LinearBuffer> out;
    EXPECT_EQ(pull_frame(rx, pool, out), PullResult::too_long);
}

TEST(WireFrameExtractTest, GoodFrameExtractedWholePayload) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    push_frame(rx, 0x42, "abc");

    PoolHandle<LinearBuffer> out;
    ASSERT_EQ(pull_frame(rx, pool, out), PullResult::ok);
    ASSERT_TRUE(static_cast<bool>(out));
    auto const d = out->data_span();
    ASSERT_EQ(d.size(), 4u); // `[type][body]`
    EXPECT_EQ(static_cast<std::uint8_t>(d[0]), 0x42u);
    EXPECT_EQ(std::memcmp(d.data() + 1, "abc", 3), 0);
    EXPECT_EQ(rx.size(), 0u); // 완전히 소비됨
}

TEST(WireFrameExtractTest, ZeroLengthFrameExtracted) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    auto const hb = frame::encode(0); // payload 0바이트
    write_bytes(rx, {hb.data(), hb.size()});

    PoolHandle<LinearBuffer> out;
    ASSERT_EQ(pull_frame(rx, pool, out), PullResult::ok);
    ASSERT_TRUE(static_cast<bool>(out));
    EXPECT_EQ(out->data_span().size(), 0u);
    EXPECT_EQ(rx.size(), 0u);
}

TEST(WireFrameExtractTest, MultipleFramesExtractedSequentially) {
    CircularBuffer rx{1024};
    auto pool = make_pool();
    push_frame(rx, 0x01, "one");
    push_frame(rx, 0x02, "two");

    PoolHandle<LinearBuffer> a;
    PoolHandle<LinearBuffer> b;
    PoolHandle<LinearBuffer> c;
    ASSERT_EQ(pull_frame(rx, pool, a), PullResult::ok);
    ASSERT_EQ(pull_frame(rx, pool, b), PullResult::ok);
    EXPECT_EQ(pull_frame(rx, pool, c), PullResult::incomplete); // 둘 다 소비 후 빈 ring

    EXPECT_EQ(static_cast<std::uint8_t>(a->data_span()[0]), 0x01u);
    EXPECT_EQ(static_cast<std::uint8_t>(b->data_span()[0]), 0x02u);
}

} // namespace
