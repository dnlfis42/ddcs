#include "ddcs/device/mode.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::device::decode_mode;
using ddcs::device::encode_mode;
using ddcs::device::Mode;

// wire 어휘 계약: enum underlying 값과 독립적으로 mode <-> byte 매핑을 고정한다.
// - 이 계약이 깨지면 enum 순서를 바꿨을 때 encode_mode/decode_mode가 여기서 잡힌다.
TEST(ModeTest, EncodesModeToWireContract) {
    EXPECT_EQ(encode_mode(Mode::safe), 0);
    EXPECT_EQ(encode_mode(Mode::normal), 1);
    EXPECT_EQ(encode_mode(Mode::performance), 2);
}

TEST(ModeTest, DecodesWireContractToMode) {
    EXPECT_EQ(decode_mode(0), Mode::safe);
    EXPECT_EQ(decode_mode(1), Mode::normal);
    EXPECT_EQ(decode_mode(2), Mode::performance);
}

TEST(ModeTest, RoundTripsEveryMode) {
    for (Mode const m : {Mode::safe, Mode::normal, Mode::performance}) {
        EXPECT_EQ(decode_mode(encode_mode(m)), m);
    }
}

TEST(ModeTest, RejectsOutOfVocabularyWireByte) {
    for (std::uint8_t const raw : {std::uint8_t{0x03}, std::uint8_t{0xFF}}) {
        EXPECT_FALSE(decode_mode(raw).has_value()) << "raw=" << static_cast<int>(raw);
    }
}

} // namespace
