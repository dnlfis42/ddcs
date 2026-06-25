#include "ddcs/common/env.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::common::parse_port;

TEST(EnvParsePortTest, ParsesValidPorts) {
    EXPECT_EQ(parse_port("8080").value_or(0), std::uint16_t{8080});
    EXPECT_EQ(parse_port("1").value_or(0), std::uint16_t{1});
    EXPECT_EQ(parse_port("65535").value_or(0), std::uint16_t{65535});
}

TEST(EnvParsePortTest, RejectsInvalidDeterministically) {
    EXPECT_FALSE(parse_port("0").has_value());           // 0은 포트로 무의미
    EXPECT_FALSE(parse_port("65536").has_value());       // 범위 초과
    EXPECT_FALSE(parse_port("99999999999").has_value()); // overflow(atoi였다면 UB)
    EXPECT_FALSE(parse_port("-1").has_value());          // 음수
    EXPECT_FALSE(parse_port("8080abc").has_value());     // 후행 garbage(atoi는 8080으로 통과시킴)
    EXPECT_FALSE(parse_port("8080 ").has_value());       // 후행 공백
    EXPECT_FALSE(parse_port(" 8080").has_value());       // 선행 공백
    EXPECT_FALSE(parse_port("0x1F90").has_value());      // 16진 표기(atoi는 0)
    EXPECT_FALSE(parse_port("").has_value());            // 빈 문자열
    EXPECT_FALSE(parse_port("abc").has_value());         // 비숫자
}

} // namespace
