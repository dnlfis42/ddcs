#include "ddcs/config/file.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using namespace ddcs;

json::Value parse_text(std::string_view text) {
    auto parsed = json::parse(text);
    EXPECT_TRUE(parsed.has_value());
    return std::move(*parsed);
}

// 병렬 ctest(debug/asan 동시 실행)가 충돌하지 않게 프로세스별 고유 이름을 쓴다
std::filesystem::path temp_file(std::string_view name, std::string_view content) {
    auto path = std::filesystem::temp_directory_path() /
                (std::to_string(::getpid()) + "_" + std::string{name});
    std::ofstream out{path};
    out << content;
    return path;
}

TEST(FileLoadTest, ReturnsNulloptWhenMissing) {
    EXPECT_FALSE(config::file::load("/nonexistent/ddcs.json").has_value());
}

TEST(FileLoadTest, LoadsAndParsesFile) {
    auto const path = temp_file("ddcs_config_test.json", R"({"port":9090})");
    auto const root = config::file::load(path);
    ASSERT_TRUE(root.has_value());
    EXPECT_EQ(config::file::get_port(*root, "port", 1), 9090);
    std::filesystem::remove(path);
}

TEST(FileLoadTest, ThrowsOnMalformedJson) {
    auto const path = temp_file("ddcs_config_bad.json", "{not json");
    EXPECT_THROW((void)config::file::load(path), std::runtime_error);
    std::filesystem::remove(path);
}

TEST(FileLookupTest, ReadsNestedDottedPath) {
    auto const root =
        parse_text(R"({"session":{"handshake_timeout_ms":250},"log":{"level":"warn"}})");
    EXPECT_EQ(
        config::file::get_duration_ms(
            root, "session.handshake_timeout_ms", std::chrono::seconds{3}
        ),
        std::chrono::milliseconds{250}
    );
    EXPECT_EQ(config::file::get_string(root, "log.level", "info"), "warn");
}

TEST(FileLookupTest, FallsBackOnMissingKey) {
    auto const root = parse_text(R"({})");
    EXPECT_EQ(config::file::get_string(root, "host", "def"), "def");
    EXPECT_EQ(config::file::get_int(root, "count", 7), 7);
    EXPECT_EQ(config::file::get_port(root, "port", 8080), 8080);
    EXPECT_EQ(
        config::file::get_duration_ms(root, "timeout_ms", std::chrono::milliseconds{1000}),
        std::chrono::milliseconds{1000}
    );
}

TEST(FileLookupTest, FallsBackOnTypeMismatch) {
    auto const root = parse_text(R"({"port":"not-a-number","count":"x"})");
    EXPECT_EQ(config::file::get_port(root, "port", 8080), 8080);
    EXPECT_EQ(config::file::get_int(root, "count", 7), 7);
}

TEST(FileLookupTest, FallsBackOnOutOfRange) {
    auto const root = parse_text(R"({"count":3000000000,"port":70000,"timeout_ms":-5})");
    EXPECT_EQ(config::file::get_int(root, "count", 7), 7);       // int 범위 초과
    EXPECT_EQ(config::file::get_port(root, "port", 8080), 8080); // 1..65535 밖
    EXPECT_EQ(
        config::file::get_duration_ms(root, "timeout_ms", std::chrono::milliseconds{1000}),
        std::chrono::milliseconds{1000} // 음수 시한
    );
    // 흡수된 port 범위 검사의 경계: 0과 65536은 거부, 1과 65535는 통과
    auto const edge = parse_text(R"({"zero":0,"min":1,"max":65535,"over":65536})");
    EXPECT_EQ(config::file::get_port(edge, "zero", 8080), 8080);
    EXPECT_EQ(config::file::get_port(edge, "min", 8080), 1);
    EXPECT_EQ(config::file::get_port(edge, "max", 8080), 65535);
    EXPECT_EQ(config::file::get_port(edge, "over", 8080), 8080);
}

} // namespace
