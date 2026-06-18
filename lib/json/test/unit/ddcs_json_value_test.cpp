#include "ddcs/json/value.hpp"
#include "ddcs/json/writer.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ddcs::json::parse;
using ddcs::json::Value;

TEST(JsonValueTest, ScalarTypePredicatesAndAccessors) {
    EXPECT_TRUE(Value{}.is_null());
    EXPECT_TRUE(Value{nullptr}.is_null());

    EXPECT_TRUE(Value{true}.is_bool());
    EXPECT_EQ(Value{true}.as_bool(), std::optional<bool>{true});

    EXPECT_TRUE(Value{42}.is_number());
    EXPECT_EQ(Value{42}.as_int64(), std::optional<std::int64_t>{42});
    EXPECT_EQ(Value{42}.as_double(), std::optional<double>{42.0}); // 정수도 double로

    EXPECT_TRUE(Value{3.5}.is_number());
    EXPECT_EQ(Value{3.5}.as_double(), std::optional<double>{3.5});
    EXPECT_EQ(Value{3.5}.as_int64(), std::nullopt); // double이면 as_int64는 strict하게 거절

    EXPECT_TRUE(Value{"hi"}.is_string());
    EXPECT_EQ(Value{"hi"}.as_string(), std::optional<std::string_view>{"hi"});
}

TEST(JsonValueTest, AccessorsReturnNulloptOnTypeMismatch) {
    EXPECT_EQ(Value{42}.as_bool(), std::nullopt);
    EXPECT_EQ(Value{"x"}.as_double(), std::nullopt);
    EXPECT_EQ(Value{true}.as_string(), std::nullopt);
    EXPECT_EQ(Value{}.as_int64(), std::nullopt);
}

TEST(JsonValueTest, ObjectSetFindContainsPreservesOrder) {
    Value obj = Value::object();
    obj.set("cpu", 42.5).set("mode", "perf").set("count", 7);

    EXPECT_TRUE(obj.is_object());
    EXPECT_EQ(obj.size(), 3u);
    ASSERT_NE(obj.find("cpu"), nullptr);

    EXPECT_EQ(obj.find("cpu")->as_double(), std::optional<double>{42.5});
    EXPECT_EQ(obj.find("mode")->as_string(), std::optional<std::string_view>{"perf"});
    EXPECT_TRUE(obj.contains("count"));
    EXPECT_FALSE(obj.contains("absent"));
    EXPECT_EQ(obj.find("absent"), nullptr);

    // 삽입 순서 보존 확인(dump로)
    EXPECT_EQ(obj.dump(), R"({"cpu":42.5,"mode":"perf","count":7})");
}

TEST(JsonValueTest, SetUpdatesInPlaceKeepingPosition) {
    Value obj = Value::object();
    obj.set("a", 1).set("b", 2).set("a", 99); // a 갱신, 위치 유지

    EXPECT_EQ(obj.size(), 2u);
    EXPECT_EQ(obj.find("a")->as_int64(), std::optional<std::int64_t>{99});
    EXPECT_EQ(obj.dump(), R"({"a":99,"b":2})");
}

TEST(JsonValueTest, SetOnNullPromotesToObject) {
    Value v; // null
    v.set("k", "x");

    EXPECT_TRUE(v.is_object());
    EXPECT_EQ(v.dump(), R"({"k":"x"})");
}

TEST(JsonValueTest, TrySetReturnsFalseOnNonObject) {
    Value v{"text"};

    EXPECT_FALSE(v.try_set("k", "x"));
    EXPECT_TRUE(v.is_string());
    EXPECT_EQ(v.as_string(), std::optional<std::string_view>{"text"});
}

TEST(JsonValueTest, ForEachMemberIteratesObjectInInsertionOrder) {
    Value v = Value::object();
    v.set("a", 1).set("b", "x").set("c", true);

    std::vector<std::string> keys;
    v.for_each_member([&](std::string_view k, Value const&) { keys.emplace_back(k); });

    ASSERT_EQ(keys.size(), 3u);

    EXPECT_EQ(keys[0], "a");
    EXPECT_EQ(keys[1], "b");
    EXPECT_EQ(keys[2], "c");

    int n = 0; // non-object면 no-op
    Value{42}.for_each_member([&](std::string_view, Value const&) { ++n; });

    EXPECT_EQ(n, 0);
}

TEST(JsonValueTest, ArrayPushBackAndIndex) {
    Value arr = Value::array();
    arr.push_back(1).push_back("two").push_back(3.0);

    EXPECT_TRUE(arr.is_array());
    EXPECT_EQ(arr.size(), 3u);
    ASSERT_NE(arr.at(1), nullptr);

    EXPECT_EQ(arr.at(1)->as_string(), std::optional<std::string_view>{"two"});
    EXPECT_EQ(arr.at(3), nullptr); // 범위 밖
}

TEST(JsonValueTest, PushBackOnNullPromotesToArray) {
    Value v;
    v.push_back(1);

    EXPECT_TRUE(v.is_array());
    EXPECT_EQ(v.dump(), "[1]");
}

TEST(JsonValueTest, TryPushBackReturnsFalseOnNonArray) {
    Value v{"text"};

    EXPECT_FALSE(v.try_push_back(1));
    EXPECT_TRUE(v.is_string());
    EXPECT_EQ(v.as_string(), std::optional<std::string_view>{"text"});
}

TEST(JsonValueTest, DumpsScalarsAndEmptyContainers) {
    EXPECT_EQ(Value{}.dump(), "null");
    EXPECT_EQ(Value{true}.dump(), "true");
    EXPECT_EQ(Value{false}.dump(), "false");
    EXPECT_EQ(Value{-17}.dump(), "-17");
    EXPECT_EQ(Value{3.5}.dump(), "3.5");
    EXPECT_EQ(Value{"hello"}.dump(), R"("hello")");
    EXPECT_EQ(Value::object().dump(), "{}");
    EXPECT_EQ(Value::array().dump(), "[]");
}

TEST(JsonValueTest, DumpEscapesStringSpecials) {
    EXPECT_EQ(Value{std::string{"a\"b\\c"}}.dump(), R"("a\"b\\c")");
    EXPECT_EQ(Value{std::string{"line\ntab\t"}}.dump(), R"("line\ntab\t")");
    EXPECT_EQ(Value{std::string{"\b\f\r"}}.dump(), R"("\b\f\r")");
    EXPECT_EQ(Value{std::string{"\x01"}}.dump(), R"("\u0001")"); // 제어문자
    EXPECT_EQ(Value{std::string{"한글"}}.dump(), "\"한글\"");    // UTF-8 통과
}

TEST(JsonValueTest, DumpsNonFiniteDoublesAsNull) {
    EXPECT_EQ(Value{std::numeric_limits<double>::infinity()}.dump(), "null");
    EXPECT_EQ(Value{-std::numeric_limits<double>::infinity()}.dump(), "null");
    EXPECT_EQ(Value{std::numeric_limits<double>::quiet_NaN()}.dump(), "null");
}

TEST(JsonWriterTest, AppendsPrimitiveTokens) {
    std::string out;
    ddcs::json::append_null(out);
    out.push_back(' ');
    ddcs::json::append_bool(out, true);
    out.push_back(' ');
    ddcs::json::append_number(out, std::int64_t{-7});
    out.push_back(' ');
    ddcs::json::append_number(out, 3.5);

    EXPECT_EQ(out, "null true -7 3.5");
}

TEST(JsonWriterTest, AppendsStringLiteralEscapes) {
    std::string out;
    ddcs::json::append_string_literal(out, "a\"b\\c\nd\te");

    EXPECT_EQ(out, R"("a\"b\\c\nd\te")");
}

TEST(JsonWriterTest, AppendsNonFiniteDoublesAsNull) {
    std::string out;
    ddcs::json::append_number(out, std::numeric_limits<double>::infinity());
    out.push_back(' ');
    ddcs::json::append_number(out, -std::numeric_limits<double>::infinity());
    out.push_back(' ');
    ddcs::json::append_number(out, std::numeric_limits<double>::quiet_NaN());

    EXPECT_EQ(out, "null null null");
}

TEST(JsonValueTest, DumpNestedStructure) {
    Value root = Value::object();
    Value inner = Value::object();
    inner.set("threshold", 80);
    Value groups = Value::array();
    groups.push_back("a").push_back("b");
    root.set("rule", std::move(inner)).set("groups", std::move(groups));

    EXPECT_EQ(root.dump(), R"({"rule":{"threshold":80},"groups":["a","b"]})");
}

TEST(JsonValueTest, ParsesScalars) {
    EXPECT_TRUE(parse("null")->is_null());
    EXPECT_EQ(parse("true")->as_bool(), std::optional<bool>{true});
    EXPECT_EQ(parse("false")->as_bool(), std::optional<bool>{false});
    EXPECT_EQ(parse("0")->as_int64(), std::optional<std::int64_t>{0});
    EXPECT_EQ(parse("-0")->as_int64(), std::optional<std::int64_t>{0});
    EXPECT_EQ(parse("  42 ")->as_int64(), std::optional<std::int64_t>{42}); // 주변 공백
    EXPECT_EQ(parse("-7")->as_int64(), std::optional<std::int64_t>{-7});
    EXPECT_EQ(parse("3.14")->as_double(), std::optional<double>{3.14});
    EXPECT_EQ(parse("0.125")->as_double(), std::optional<double>{0.125});
    EXPECT_EQ(parse("-1.25e-2")->as_double(), std::optional<double>{-1.25e-2});
    EXPECT_EQ(parse("1e3")->as_double(), std::optional<double>{1000.0});
    EXPECT_EQ(parse(R"("text")")->as_string(), std::optional<std::string_view>{"text"});
}

TEST(JsonValueTest, ParseDistinguishesIntAndDouble) {
    EXPECT_TRUE(parse("42")->as_int64().has_value()); // 정수
    EXPECT_EQ(parse("42")->as_double(), std::optional<double>{42.0});
    EXPECT_FALSE(parse("42.0")->as_int64().has_value()); // '.' 있으면 double
    EXPECT_EQ(parse("42.0")->as_double(), std::optional<double>{42.0});
}

TEST(JsonValueTest, ParsesStringEscapesAndUnicode) {
    EXPECT_EQ(parse(R"("a\"b")")->as_string(), std::optional<std::string_view>{"a\"b"});
    EXPECT_EQ(parse(R"("\n\t")")->as_string(), std::optional<std::string_view>{"\n\t"});
    EXPECT_EQ(parse(R"("\b\f\r\/\\")")->as_string(), std::optional<std::string_view>{"\b\f\r/\\"});
    EXPECT_EQ(parse(R"("\u0041")")->as_string(), std::optional<std::string_view>{"A"}); // BMP
    EXPECT_EQ(parse(R"("한글")")->as_string(), std::optional<std::string_view>{"한글"});

    // 서로게이트 쌍 (U+1F600)
    EXPECT_EQ(
        parse(R"("\uD83D\uDE00")")->as_string(), std::optional<std::string_view>{"\U0001F600"}
    );
}

TEST(JsonValueTest, ParsesNestedObjectAndArray) {
    auto v = parse(R"({"a":[1,2,{"b":true}],"c":null})");
    ASSERT_TRUE(v.has_value());

    ASSERT_NE(v->find("a"), nullptr);

    EXPECT_EQ(v->find("a")->size(), 3u);
    EXPECT_EQ(v->find("a")->at(1)->as_int64(), std::optional<std::int64_t>{2});
    EXPECT_EQ(v->find("a")->at(2)->find("b")->as_bool(), std::optional<bool>{true});
    EXPECT_TRUE(v->find("c")->is_null());
}

TEST(JsonValueTest, ParsesWhitespaceInContainers) {
    auto v = parse(" \n { \"a\" : [ true , null , 3 ] } \t ");
    ASSERT_TRUE(v.has_value());

    auto const* a = v->find("a");
    ASSERT_NE(a, nullptr);

    EXPECT_EQ(a->size(), 3u);
    EXPECT_EQ(a->at(0)->as_bool(), std::optional<bool>{true});
    EXPECT_TRUE(a->at(1)->is_null());
    EXPECT_EQ(a->at(2)->as_int64(), std::optional<std::int64_t>{3});
}

TEST(JsonValueTest, RoundTripsThroughDumpAndParse) {
    Value root = Value::object();
    root.set("cpu", 42.5).set("mem", 60).set("mode", "perf").set("ok", true).set("note", nullptr);
    Value tags = Value::array();
    tags.push_back("x").push_back("y");
    root.set("tags", std::move(tags));

    auto reparsed = parse(root.dump());
    ASSERT_TRUE(reparsed.has_value());

    EXPECT_EQ(*reparsed, root);               // 값 동등
    EXPECT_EQ(reparsed->dump(), root.dump()); // 텍스트 동등
}

TEST(JsonValueTest, RejectsMalformedInput) {
    EXPECT_EQ(parse(""), std::nullopt);            // 빈 입력
    EXPECT_EQ(parse("  "), std::nullopt);          // 공백뿐
    EXPECT_EQ(parse("42 garbage"), std::nullopt);  // trailing
    EXPECT_EQ(parse("{"), std::nullopt);           // 미완 object
    EXPECT_EQ(parse("[1,2"), std::nullopt);        // 미완 array
    EXPECT_EQ(parse(R"({"a":1,})"), std::nullopt); // trailing comma
    EXPECT_EQ(parse(R"({"a" 1})"), std::nullopt);  // colon 없음
    EXPECT_EQ(parse(R"("unterminated)"), std::nullopt);
    EXPECT_EQ(parse(R"("bad\xescape")"), std::nullopt); // 미지 escape
    EXPECT_EQ(parse(R"("\uD83D")"), std::nullopt);      // 외톨이 high surrogate
    EXPECT_EQ(parse(R"("\uDE00")"), std::nullopt);      // 외톨이 low surrogate
    EXPECT_EQ(parse(R"("\uD83D\u0041")"), std::nullopt);
    EXPECT_EQ(parse(R"("\u12x4")"), std::nullopt);
    EXPECT_EQ(parse("tru"), std::nullopt);    // 잘린 리터럴
    EXPECT_EQ(parse("{42:1}"), std::nullopt); // 비문자열 키
}

TEST(JsonValueTest, RejectsInvalidNumberSyntax) {
    EXPECT_EQ(parse("-"), std::nullopt);
    EXPECT_EQ(parse("+1"), std::nullopt);
    EXPECT_EQ(parse(".1"), std::nullopt);
    EXPECT_EQ(parse("--1"), std::nullopt);
    EXPECT_EQ(parse("00"), std::nullopt);
    EXPECT_EQ(parse("01"), std::nullopt);
    EXPECT_EQ(parse("-01"), std::nullopt);
    EXPECT_EQ(parse("1."), std::nullopt);
    EXPECT_EQ(parse("1.e2"), std::nullopt);
    EXPECT_EQ(parse("1e"), std::nullopt);
    EXPECT_EQ(parse("1e+"), std::nullopt);
    EXPECT_EQ(parse("[1,01]"), std::nullopt);
}

TEST(JsonValueTest, RejectsExcessiveNesting) {
    std::string deep;
    for (int i = 0; i < 200; ++i) {
        deep += '[';
    }

    EXPECT_EQ(parse(deep), std::nullopt); // max_depth 초과 시 nullopt(스택오버플로 방어)
}

} // namespace
