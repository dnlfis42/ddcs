#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ddcs::json {

// 재귀 JSON 값 DOM. 6 타입: null / bool / number(int64 또는 double) / string / array / object.
// object는 삽입 순서를 보존한다(vector<pair>, 키 5~10개 규모라 선형 검색이 단순/빠름).
// wire/파일 경계에서만 문자열(dump/parse), 작업은 이 타입으로. 접근은 비-throw(optional/nullptr).
class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    Value() noexcept;               // null
    Value(std::nullptr_t) noexcept; // null
    Value(bool b) noexcept;
    Value(int n) noexcept;           // int64로 (int 리터럴이 int64/double 사이 모호해지지 않게)
    Value(std::int64_t n) noexcept;  // 정수
    Value(std::uint64_t n) noexcept; // int64로 (캐스트; 우리 도메인 범위 안전)
    Value(double d) noexcept;        // 실수
    Value(char const* s);            // string으로
    Value(std::string s);            // string으로

    static Value object(); // 빈 object
    static Value array();  // 빈 array

    // 타입 질의
    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_number() const noexcept; // int64 또는 double
    bool is_string() const noexcept;
    bool is_array() const noexcept;
    bool is_object() const noexcept;

    // 타입 안전 접근. 타입 불일치 시 nullopt(비-throw). as_string의 view는 이 Value의 수명에 묶임.
    std::optional<bool> as_bool() const noexcept;
    std::optional<std::int64_t> as_int() const noexcept; // int64 일 때만
    std::optional<double> as_double() const noexcept;    // int64|double을 double로 (정수도 허용)
    std::optional<std::string_view> as_string() const noexcept;

    // object 연산 (삽입순 보존). null이면 set 시 object로 승격.
    Value& set(std::string key, Value v);                   // 키 있으면 갱신(위치 유지), 없으면 append. *this 반환
    Value const* find(std::string_view key) const noexcept; // 없으면 nullptr (첫 일치)
    bool contains(std::string_view key) const noexcept;

    // object 멤버 순회(삽입순): fn(std::string_view key, Value const& value). object 아니면 no-op.
    // (동적 키 object 파싱용. find로는 키 목록을 모름.)
    template <typename Fn>
    void for_each_member(Fn&& fn) const {
        if (auto const* obj = std::get_if<Object>(&data_)) {
            for (auto const& [key, value] : *obj) {
                fn(std::string_view{key}, value);
            }
        }
    }

    // array 연산. null이면 push_back 시 array로 승격.
    Value& push_back(Value v);
    std::size_t size() const noexcept;             // array/object 원소 수 (그 외 0)
    Value const* at(std::size_t i) const noexcept; // array 인덱스 (범위 밖 nullptr)

    // 직렬화 / 역직렬화
    std::string dump() const;                            // compact(공백 없음)
    static std::optional<Value> parse(std::string_view); // 실패(malformed/trailing/과도한 중첩) 시 nullopt

    bool operator==(Value const&) const = default;

private:
    void dump_to(std::string& out) const; // 재귀 직렬화

    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> data_;
};

} // namespace ddcs::json
