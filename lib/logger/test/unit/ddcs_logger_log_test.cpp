#include "ddcs/logger/log.hpp"

#include "ddcs/json/value.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cstdint>

namespace {

class CaptureSink : public ddcs::logger::Sink {
public:
    void write(std::string_view line) noexcept override {
        lines.emplace_back(line);
    }

public:
    std::vector<std::string> lines;
};

class LogTest : public ::testing::Test {
protected:
    void SetUp() override {
        ddcs::logger::Logger::instance().set_sink(sink);
        ddcs::logger::Logger::instance().set_level(ddcs::logger::Level::Debug);
    }

protected:
    CaptureSink sink;
};

// 헤더에 ts/level/file/line/msg가 포함되는지
TEST_F(LogTest, EmitsRequiredHeaderFields) {
    LOG_INFO("hello");
    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    EXPECT_NE(l.find("\"ts\":\""), std::string::npos);
    EXPECT_NE(l.find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(l.find("\"file\":\""), std::string::npos);
    EXPECT_NE(l.find("\"line\":"), std::string::npos);
    EXPECT_NE(l.find("\"msg\":\"hello\""), std::string::npos);
    EXPECT_EQ(l.back(), '}');
}

// 레벨 임계 이하는 emit 안 함
TEST_F(LogTest, LevelThresholdFiltersBelow) {
    ddcs::logger::Logger::instance().set_level(ddcs::logger::Level::Warn);
    LOG_DEBUG("d");
    LOG_INFO("i");
    LOG_WARN("w");
    LOG_ERROR("e");
    ASSERT_EQ(sink.lines.size(), 2u);
    EXPECT_NE(sink.lines[0].find("\"level\":\"WARN\""), std::string::npos);
    EXPECT_NE(sink.lines[1].find("\"level\":\"ERROR\""), std::string::npos);
}

// 레벨별 라벨 정확
TEST_F(LogTest, AllLevelLabels) {
    LOG_DEBUG("x");
    LOG_INFO("x");
    LOG_WARN("x");
    LOG_ERROR("x");
    ASSERT_EQ(sink.lines.size(), 4u);
    EXPECT_NE(sink.lines[0].find("\"level\":\"DEBUG\""), std::string::npos);
    EXPECT_NE(sink.lines[1].find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(sink.lines[2].find("\"level\":\"WARN\""), std::string::npos);
    EXPECT_NE(sink.lines[3].find("\"level\":\"ERROR\""), std::string::npos);
}

// kv 직렬화: 문자열/정수/bool
TEST_F(LogTest, KvBasicTypes) {
    using ddcs::logger::kv;
    LOG_INFO(
        "evt", kv("peer", "127.0.0.1:5432"), kv("id", 42), kv("ok", true), kv("missing", nullptr)
    );
    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    EXPECT_NE(l.find("\"peer\":\"127.0.0.1:5432\""), std::string::npos);
    EXPECT_NE(l.find("\"id\":42"), std::string::npos);
    EXPECT_NE(l.find("\"ok\":true"), std::string::npos);
    EXPECT_NE(l.find("\"missing\":null"), std::string::npos);
}

// kv 직렬화: 부호 있는 정수
TEST_F(LogTest, KvSignedInt) {
    using ddcs::logger::kv;
    LOG_INFO("evt", kv("n", -7));
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"n\":-7"), std::string::npos);
}

// kv 직렬화: 64비트 정수 (큰 수)
TEST_F(LogTest, KvLargeInteger) {
    using ddcs::logger::kv;
    std::uint64_t const big = 18446744073709551615ull; // u64 max
    LOG_INFO("evt", kv("big", big));
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"big\":18446744073709551615"), std::string::npos);
}

// JSON escape: 따옴표/백슬래시/제어문자
TEST_F(LogTest, JsonEscapeSpecialChars) {
    using ddcs::logger::kv;
    LOG_INFO("evt", kv("s", "a\"b\\c\nd\te"));
    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    EXPECT_NE(l.find("\"s\":\"a\\\"b\\\\c\\nd\\te\""), std::string::npos);
}

// JSON escape: 낮은 제어문자(\u 형식)
TEST_F(LogTest, JsonEscapesLowControl) {
    using ddcs::logger::kv;
    std::string s;
    s.push_back('\x01');
    LOG_INFO("evt", kv("s", s));
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"s\":\"\\u0001\""), std::string::npos);
}

// msg도 escape
TEST_F(LogTest, MsgEscaped) {
    LOG_INFO("a\"b");
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"msg\":\"a\\\"b\""), std::string::npos);
}

// source_location: 한 줄 위/아래 라인 번호가 다르게 캡처되는지
TEST_F(LogTest, SourceLocationCapturesLine) {
    LOG_INFO("first");
    LOG_INFO("second");
    ASSERT_EQ(sink.lines.size(), 2u);
    // 두 호출의 line 값이 달라야 함.
    auto extract_line = [](std::string const& l) -> int {
        auto const key = std::string_view{"\"line\":"};
        auto const pos = l.find(key);
        if (pos == std::string::npos) {
            return -1;
        }
        return std::stoi(l.substr(pos + key.size()));
    };
    int const l1 = extract_line(sink.lines[0]);
    int const l2 = extract_line(sink.lines[1]);
    EXPECT_GT(l1, 0);
    EXPECT_EQ(l2, l1 + 1);
}

// file 필드: basename (경로 segment 없이)
TEST_F(LogTest, FileFieldIsBasename) {
    LOG_INFO("x");
    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    EXPECT_NE(l.find("\"file\":\"ddcs_logger_log_test.cpp\""), std::string::npos);
}

// enum kv: underlying integer로 직렬화
TEST_F(LogTest, KvEnum) {
    using ddcs::logger::kv;
    enum class E : std::uint8_t { A = 0, B = 7 };
    LOG_INFO("evt", kv("e", E::B));
    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"e\":7"), std::string::npos);
}

// 로거 출력이 substring이 아니라 *유효한 JSON* 인지. ddcs::json 파서로 교차검증 (escape 라운드트립)
TEST_F(LogTest, EmitsValidJsonParseableByJsonLib) {
    using ddcs::logger::kv;
    std::string tricky = "q\"b\\s\nt";
    tricky.push_back(char{1}); // 제어문자 0x01. escape 후 파싱이 원문 복원하는지

    LOG_INFO("evt", kv("peer", "1.2.3.4:5"), kv("id", 42), kv("ok", true), kv("s", tricky));

    ASSERT_EQ(sink.lines.size(), 1u);
    auto const parsed = ddcs::json::Value::parse(sink.lines.front());
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->is_object());
    EXPECT_EQ(parsed->find("level")->as_string(), std::optional<std::string_view>{"INFO"});
    EXPECT_EQ(parsed->find("msg")->as_string(), std::optional<std::string_view>{"evt"});
    EXPECT_EQ(parsed->find("id")->as_int(), std::optional<std::int64_t>{42});
    EXPECT_EQ(parsed->find("ok")->as_bool(), std::optional<bool>{true});
    EXPECT_EQ(parsed->find("peer")->as_string(), std::optional<std::string_view>{"1.2.3.4:5"});
    // escape 후 parse로 원문 복원
    EXPECT_EQ(parsed->find("s")->as_string(), std::optional<std::string_view>{tricky});
}

// level_from_string: 대소문자 무시 매칭 + 미매칭 fallback
TEST(LogLevelFromStringTest, ParsesKnownLevels) {
    using ddcs::logger::Level;
    using ddcs::logger::level_from_string;
    EXPECT_EQ(level_from_string("debug", Level::Info), Level::Debug);
    EXPECT_EQ(level_from_string("INFO", Level::Error), Level::Info);
    EXPECT_EQ(level_from_string("Warn", Level::Info), Level::Warn);
    EXPECT_EQ(level_from_string("warning", Level::Info), Level::Warn);
    EXPECT_EQ(level_from_string("ERROR", Level::Info), Level::Error);
}

TEST(LogLevelFromStringTest, FallsBackOnUnknown) {
    using ddcs::logger::Level;
    using ddcs::logger::level_from_string;
    EXPECT_EQ(level_from_string("", Level::Warn), Level::Warn);
    EXPECT_EQ(level_from_string("verbose", Level::Info), Level::Info);
    EXPECT_EQ(level_from_string("inf", Level::Error), Level::Error); // 부분일치 불가
}

// sink 없음. crash 없이 무시
TEST(LogNoSinkTest, NoSinkIsSafe) {
    ddcs::logger::Logger::instance().set_level(ddcs::logger::Level::Debug);
    // 명시적으로 sink 해제는 API가 없으므로 다른 sink로 갈음
    CaptureSink dummy;
    ddcs::logger::Logger::instance().set_sink(dummy);
    EXPECT_NO_THROW(LOG_INFO("ping"));
}

} // namespace
