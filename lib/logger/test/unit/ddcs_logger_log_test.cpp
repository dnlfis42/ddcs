#include "ddcs/logger/log.hpp"

#include "ddcs/json/value.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

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
        ddcs::logger::Logger::instance().set_level(ddcs::logger::Level::debug);
    }

protected:
    CaptureSink sink;
};

TEST_F(LogTest, EmitsRequiredHeaderFields) {
    LOG_INFO("hello");

    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    EXPECT_NE(l.find("\"ts\":\""), std::string::npos);
    EXPECT_NE(l.find("\"level\":\"INFO\""), std::string::npos);
    EXPECT_NE(l.find("\"file\":\""), std::string::npos);
    EXPECT_NE(l.find("\"line\":"), std::string::npos);
    EXPECT_NE(l.find("\"event\":\"hello\""), std::string::npos);
    EXPECT_EQ(l.back(), '}');
}

TEST_F(LogTest, FiltersBelowThreshold) {
    ddcs::logger::Logger::instance().set_level(ddcs::logger::Level::warn);

    LOG_DEBUG("d");
    LOG_INFO("i");
    LOG_WARN("w");
    LOG_ERROR("e");

    ASSERT_EQ(sink.lines.size(), 2u);
    EXPECT_NE(sink.lines[0].find("\"level\":\"WARN\""), std::string::npos);
    EXPECT_NE(sink.lines[1].find("\"level\":\"ERROR\""), std::string::npos);
}

TEST_F(LogTest, LabelsEachLevel) {
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

TEST_F(LogTest, SerializesBasicKvTypes) {
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

TEST_F(LogTest, SerializesSignedInt) {
    using ddcs::logger::kv;
    LOG_INFO("evt", kv("n", -7));

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"n\":-7"), std::string::npos);
}

TEST_F(LogTest, SerializesUint64Max) {
    using ddcs::logger::kv;
    std::uint64_t const big = 18446744073709551615ull; // u64 max

    LOG_INFO("evt", kv("big", big));

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"big\":18446744073709551615"), std::string::npos);
}

TEST_F(LogTest, EscapesSpecialChars) {
    using ddcs::logger::kv;
    LOG_INFO("evt", kv("s", "a\"b\\c\nd\te"));

    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    EXPECT_NE(l.find("\"s\":\"a\\\"b\\\\c\\nd\\te\""), std::string::npos);
}

TEST_F(LogTest, EscapesLowControlChars) {
    using ddcs::logger::kv;
    std::string s;
    s.push_back('\x01');

    LOG_INFO("evt", kv("s", s));

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"s\":\"\\u0001\""), std::string::npos);
}

TEST_F(LogTest, EscapesFieldKey) {
    using ddcs::logger::kv;
    LOG_INFO("evt", kv("a\"b", 7));

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"a\\\"b\":7"), std::string::npos);

    auto const parsed = ddcs::json::parse(sink.lines.front());
    ASSERT_TRUE(parsed.has_value()) << "JSON parse must succeed for logger output";
    ASSERT_NE(parsed->find("a\"b"), nullptr);
    EXPECT_EQ(parsed->find("a\"b")->as_int64(), std::optional<std::int64_t>{7});
}

TEST_F(LogTest, EscapesEvent) {
    LOG_INFO("a\"b");

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"event\":\"a\\\"b\""), std::string::npos);
}

TEST_F(LogTest, CapturesSourceLine) {
    LOG_INFO("first");
    LOG_INFO("second");

    ASSERT_EQ(sink.lines.size(), 2u);
    // 두 호출의 line 값이 달라야 함
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

TEST_F(LogTest, EmitsFileAsBasename) {
    LOG_INFO("x");

    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    EXPECT_NE(l.find("\"file\":\"ddcs_logger_log_test.cpp\""), std::string::npos);
}

// 사람이 읽는 주요 정보(event, kv)를 먼저 두고 source 위치는 마지막 metadata로 둔다.
TEST_F(LogTest, PlacesSourceLocationLast) {
    using ddcs::logger::kv;
    LOG_INFO("evt", kv("conn", 7));

    ASSERT_EQ(sink.lines.size(), 1u);
    auto const& l = sink.lines.front();
    auto const event_pos = l.find("\"event\":\"evt\"");
    auto const conn_pos = l.find("\"conn\":7");
    auto const file_pos = l.find("\"file\":\"ddcs_logger_log_test.cpp\"");
    auto const line_pos = l.find("\"line\":");

    ASSERT_NE(event_pos, std::string::npos);
    ASSERT_NE(conn_pos, std::string::npos);
    ASSERT_NE(file_pos, std::string::npos);
    ASSERT_NE(line_pos, std::string::npos);
    EXPECT_LT(event_pos, conn_pos);
    EXPECT_LT(conn_pos, file_pos);
    EXPECT_LT(file_pos, line_pos);
    EXPECT_EQ(l.back(), '}');
}

TEST_F(LogTest, SerializesEnumAsUnderlying) {
    using ddcs::logger::kv;
    enum class E : std::uint8_t { A = 0, B = 7 };

    LOG_INFO("evt", kv("e", E::B));

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"e\":7"), std::string::npos);
}

// NaN/Inf는 표준 JSON 값이 아니므로 null
TEST_F(LogTest, SerializesNonFiniteAsNull) {
    using ddcs::logger::kv;
    LOG_INFO(
        "evt", kv("inf", std::numeric_limits<double>::infinity()),
        kv("ninf", -std::numeric_limits<double>::infinity()),
        kv("nan", std::numeric_limits<double>::quiet_NaN())
    );

    ASSERT_EQ(sink.lines.size(), 1u);
    EXPECT_NE(sink.lines.front().find("\"inf\":null"), std::string::npos);
    EXPECT_NE(sink.lines.front().find("\"ninf\":null"), std::string::npos);
    EXPECT_NE(sink.lines.front().find("\"nan\":null"), std::string::npos);
}

// 전체 출력을 ddcs::json으로 파싱해 유효한 JSON인지 교차검증한다.
TEST_F(LogTest, EmitsValidJsonParseableByJsonLib) {
    using ddcs::logger::kv;
    std::string tricky = "q\"b\\s\nt";
    tricky.push_back(char{1});

    LOG_INFO("evt", kv("peer", "1.2.3.4:5"), kv("id", 42), kv("ok", true), kv("s", tricky));

    ASSERT_EQ(sink.lines.size(), 1u);
    auto const parsed = ddcs::json::parse(sink.lines.front());
    ASSERT_TRUE(parsed.has_value()) << "JSON parse must succeed for logger output";
    ASSERT_TRUE(parsed->is_object());
    EXPECT_EQ(parsed->find("level")->as_string(), std::optional<std::string_view>{"INFO"});
    EXPECT_EQ(parsed->find("event")->as_string(), std::optional<std::string_view>{"evt"});
    EXPECT_EQ(parsed->find("id")->as_int64(), std::optional<std::int64_t>{42});
    EXPECT_EQ(parsed->find("ok")->as_bool(), std::optional<bool>{true});
    EXPECT_EQ(parsed->find("peer")->as_string(), std::optional<std::string_view>{"1.2.3.4:5"});
    // escape 후 parse로 원문 복원
    EXPECT_EQ(parsed->find("s")->as_string(), std::optional<std::string_view>{tricky});
}

TEST(LogParseLevelTest, ParsesKnownLevels) {
    using ddcs::logger::Level;
    using ddcs::logger::parse_level;
    EXPECT_EQ(parse_level("debug"), std::optional<Level>{Level::debug});
    EXPECT_EQ(parse_level("INFO"), std::optional<Level>{Level::info});
    EXPECT_EQ(parse_level("Warn"), std::optional<Level>{Level::warn});
    EXPECT_EQ(parse_level("warning"), std::optional<Level>{Level::warn});
    EXPECT_EQ(parse_level("ERROR"), std::optional<Level>{Level::error});
}

TEST(LogParseLevelTest, ReturnsNulloptOnUnknown) {
    using ddcs::logger::parse_level;
    EXPECT_EQ(parse_level(""), std::nullopt);
    EXPECT_EQ(parse_level("verbose"), std::nullopt);
    EXPECT_EQ(parse_level("inf"), std::nullopt); // 부분일치 불가
}

TEST(LogNoSinkTest, ClearSinkMakesLoggingNoOp) {
    auto& lg = ddcs::logger::Logger::instance();
    lg.set_level(ddcs::logger::Level::debug);

    CaptureSink sink;
    lg.set_sink(sink);
    LOG_INFO("ping");
    ASSERT_EQ(sink.lines.size(), 1u); // 설치 상태에선 기록된다

    lg.clear_sink(sink); // detach: 이후 log()는 null guard로 no-op
    EXPECT_NO_THROW(LOG_INFO("after_detach"));
    EXPECT_EQ(sink.lines.size(), 1u); // detach 후엔 증가하지 않는다
}

} // namespace
