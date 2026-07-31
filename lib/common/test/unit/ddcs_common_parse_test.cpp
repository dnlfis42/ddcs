#include "ddcs/common/parse.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace {

using ddcs::common::parse_double;
using ddcs::common::parse_port;
using ddcs::common::parse_uuid;

TEST(ParsePortTest, ParsesValidPorts) {
    EXPECT_EQ(parse_port("8080").value_or(0), std::uint16_t{8080});
    EXPECT_EQ(parse_port("1").value_or(0), std::uint16_t{1});
    EXPECT_EQ(parse_port("65535").value_or(0), std::uint16_t{65535});
}

TEST(ParsePortTest, RejectsInvalidDeterministically) {
    EXPECT_FALSE(parse_port("0").has_value());           // 0은 포트로 무의미
    EXPECT_FALSE(parse_port("65536").has_value());       // 범위 초과
    EXPECT_FALSE(parse_port("99999999999").has_value()); // overflow(atoi였다면 UB)
    EXPECT_FALSE(parse_port("-1").has_value());          // 음수
    EXPECT_FALSE(parse_port("8080abc").has_value()); // 후행 garbage(atoi는 8080으로 통과시킴)
    EXPECT_FALSE(parse_port("8080 ").has_value());  // 후행 공백
    EXPECT_FALSE(parse_port(" 8080").has_value());  // 선행 공백
    EXPECT_FALSE(parse_port("0x1F90").has_value()); // 16진 표기(atoi는 0)
    EXPECT_FALSE(parse_port("").has_value());       // 빈 문자열
    EXPECT_FALSE(parse_port("abc").has_value());    // 비숫자
}

TEST(ParseDoubleTest, ParsesValidDoubles) {
    EXPECT_DOUBLE_EQ(parse_double("0.5").value_or(-1.0), 0.5);
    EXPECT_DOUBLE_EQ(parse_double("-0.25").value_or(-1.0), -0.25);
    EXPECT_DOUBLE_EQ(parse_double("3").value_or(-1.0), 3.0);
    EXPECT_DOUBLE_EQ(parse_double("1e-3").value_or(-1.0), 0.001);
}

TEST(ParseDoubleTest, RejectsInvalidDeterministically) {
    EXPECT_FALSE(parse_double("0.5abc").has_value()); // 후행 garbage(strtod는 0.5로 통과시킴)
    EXPECT_FALSE(parse_double("0.5 ").has_value());   // 후행 공백
    EXPECT_FALSE(parse_double(" 0.5").has_value());   // 선행 공백
    EXPECT_FALSE(parse_double("").has_value());       // 빈 문자열
    EXPECT_FALSE(parse_double("abc").has_value());    // 비숫자
}

TEST(ParseUuidTest, ParsesValidIdentity) {
    auto const u = parse_uuid("0feef128-d17f-1f55-8565-9a23ddb8c29d");
    ASSERT_TRUE(u.has_value());
    EXPECT_TRUE(u->valid());
}

TEST(ParseUuidTest, RejectsNilAndMalformed) {
    EXPECT_FALSE(parse_uuid("00000000-0000-0000-0000-000000000000").has_value()); // nil은 신원 불가
    EXPECT_FALSE(parse_uuid("not-a-uuid").has_value()); // 형식 위반
    EXPECT_FALSE(parse_uuid("").has_value());           // 빈 문자열
}

} // namespace
