#include "ddcs/config/env.hpp"

#include <cstdlib>

#include <gtest/gtest.h>

namespace {

namespace env = ddcs::config::env;

TEST(EnvGetTest, ReturnsValueWhenSet) {
    ::setenv("DDCS_TEST_ENV_STRING", "custom", 1);
    EXPECT_EQ(env::get("DDCS_TEST_ENV_STRING").value_or("fallback"), "custom");
    ::unsetenv("DDCS_TEST_ENV_STRING");
}

TEST(EnvGetTest, ReturnsNulloptWhenUnset) {
    ::unsetenv("DDCS_TEST_ENV_STRING");
    EXPECT_FALSE(env::get("DDCS_TEST_ENV_STRING").has_value());
}

TEST(EnvGetPortTest, ReturnsParsedPortWhenSet) {
    ::setenv("DDCS_TEST_ENV_PORT", "8080", 1);
    EXPECT_EQ(env::get_port("DDCS_TEST_ENV_PORT").value_or(0), std::uint16_t{8080});
    ::unsetenv("DDCS_TEST_ENV_PORT");
}

TEST(EnvGetPortTest, ReturnsNulloptWhenUnsetOrInvalid) {
    ::unsetenv("DDCS_TEST_ENV_PORT");
    EXPECT_FALSE(env::get_port("DDCS_TEST_ENV_PORT").has_value());

    ::setenv("DDCS_TEST_ENV_PORT", "8080abc", 1); // 무효면 경고 후 nullopt
    EXPECT_FALSE(env::get_port("DDCS_TEST_ENV_PORT").has_value());
    ::unsetenv("DDCS_TEST_ENV_PORT");
}

TEST(EnvGetPortTest, FallbackOverloadKeepsLowerLayerValue) {
    ::unsetenv("DDCS_TEST_ENV_PORT");
    EXPECT_EQ(env::get_port("DDCS_TEST_ENV_PORT", 9000), std::uint16_t{9000});

    ::setenv("DDCS_TEST_ENV_PORT", "8080", 1);
    EXPECT_EQ(env::get_port("DDCS_TEST_ENV_PORT", 9000), std::uint16_t{8080});

    ::setenv("DDCS_TEST_ENV_PORT", "8080abc", 1); // 무효면 경고 후 직전 층 값 유지
    EXPECT_EQ(env::get_port("DDCS_TEST_ENV_PORT", 9000), std::uint16_t{9000});
    ::unsetenv("DDCS_TEST_ENV_PORT");
}

TEST(EnvGetDoubleTest, ReturnsParsedValueWhenSet) {
    ::setenv("DDCS_TEST_ENV_DOUBLE", "0.5", 1);
    EXPECT_DOUBLE_EQ(env::get_double("DDCS_TEST_ENV_DOUBLE").value_or(1.0), 0.5);
    ::unsetenv("DDCS_TEST_ENV_DOUBLE");
}

TEST(EnvGetDoubleTest, ReturnsNulloptWhenUnsetOrInvalid) {
    ::unsetenv("DDCS_TEST_ENV_DOUBLE");
    EXPECT_FALSE(env::get_double("DDCS_TEST_ENV_DOUBLE").has_value());

    ::setenv("DDCS_TEST_ENV_DOUBLE", "0.5abc", 1); // 무효면 경고 후 nullopt
    EXPECT_FALSE(env::get_double("DDCS_TEST_ENV_DOUBLE").has_value());
    ::unsetenv("DDCS_TEST_ENV_DOUBLE");
}

TEST(EnvGetDoubleTest, FallbackOverloadKeepsLowerLayerValue) {
    ::unsetenv("DDCS_TEST_ENV_DOUBLE");
    EXPECT_DOUBLE_EQ(env::get_double("DDCS_TEST_ENV_DOUBLE", 0.25), 0.25);

    ::setenv("DDCS_TEST_ENV_DOUBLE", "0.5", 1);
    EXPECT_DOUBLE_EQ(env::get_double("DDCS_TEST_ENV_DOUBLE", 0.25), 0.5);

    ::setenv("DDCS_TEST_ENV_DOUBLE", "0.5abc", 1); // 무효면 경고 후 직전 층 값 유지
    EXPECT_DOUBLE_EQ(env::get_double("DDCS_TEST_ENV_DOUBLE", 0.25), 0.25);
    ::unsetenv("DDCS_TEST_ENV_DOUBLE");
}

} // namespace
