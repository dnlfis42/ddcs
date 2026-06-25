#include "ddcs/config/config.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#include <gtest/gtest.h>

namespace {

using ddcs::config::Config;
using namespace std::chrono_literals;

TEST(ConfigTest, ReadsNestedAndFlatKeys) {
    Config c;
    c.add_text(
        R"({"controller":{"host":"10.0.0.1","port":9000},"timeout_ms":{"handshake":2500},"backlog":64})"
    );
    EXPECT_EQ(c.get_string("controller.host", nullptr, "x"), "10.0.0.1");
    EXPECT_EQ(c.get_port("controller.port", nullptr, 1), 9000);
    EXPECT_EQ(c.get_duration_ms("timeout_ms.handshake", 9999), 2500ms);
    EXPECT_EQ(c.get_int("backlog", nullptr, 1), 64);
}

TEST(ConfigTest, LaterLayerOverridesEarlier) {
    Config c;
    c.add_text(R"({"port":8080,"log_level":"info"})");          // shared
    c.add_text(R"({"port":9090})");                             // process: port만 덮어씀
    EXPECT_EQ(c.get_port("port", nullptr, 1), 9090);            // process 우선
    EXPECT_EQ(c.get_string("log_level", nullptr, "x"), "info"); // process에 없으면 shared
}

TEST(ConfigTest, FallbackOnMissingKey) {
    Config c;
    c.add_text(R"({"a":1})");
    EXPECT_EQ(c.get_string("missing", nullptr, "def"), "def");
    EXPECT_EQ(c.get_port("nope.port", nullptr, 1234), 1234);
    EXPECT_EQ(c.get_duration_ms("nope", 500), 500ms);
}

TEST(ConfigTest, FallbackOnTypeMismatch) {
    Config c;
    c.add_text(R"({"port":"not-a-number","count":"x"})");
    EXPECT_EQ(c.get_port("port", nullptr, 8080), 8080); // string -> fallback
    EXPECT_EQ(c.get_int("count", nullptr, 7), 7);
}

TEST(ConfigTest, EnvOverridesLayerAndDefault) {
    ::setenv("DDCS_TEST_HOST", "envhost", 1);
    ::setenv("DDCS_TEST_PORT", "12345", 1);
    Config c;
    c.add_text(R"({"host":"filehost","port":8080})");
    EXPECT_EQ(c.get_string("host", "DDCS_TEST_HOST", "def"), "envhost");
    EXPECT_EQ(c.get_port("port", "DDCS_TEST_PORT", 1), 12345);
    ::unsetenv("DDCS_TEST_HOST");
    ::unsetenv("DDCS_TEST_PORT");
}

TEST(ConfigTest, InvalidEnvFallsThroughToLayer) {
    ::setenv("DDCS_TEST_BADPORT", "70000", 1); // 범위 초과 -> 무효
    Config c;
    c.add_text(R"({"port":8080})");
    // 무효 env는 무시하고 layer 값으로 흐른다(default가 아님)
    EXPECT_EQ(c.get_port("port", "DDCS_TEST_BADPORT", 9000), 8080);
    ::unsetenv("DDCS_TEST_BADPORT");
}

TEST(ConfigTest, MalformedThrows) {
    Config c;
    EXPECT_THROW(c.add_text("{not json"), std::runtime_error);
}

TEST(ConfigTest, MissingFileReturnsFalse) {
    Config c;
    EXPECT_FALSE(c.add_file("/nonexistent/path/does_not_exist.json"));
}

} // namespace
