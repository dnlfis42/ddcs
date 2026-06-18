#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace ddcs::json {

// 재귀 JSON 값 DOM
//   null, bool, number, string, array, object 6가지 JSON 값 종류를 표현한다.
//   object는 삽입 순서를 보존한다.
class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;

    Value() noexcept;
    Value(std::nullptr_t) noexcept;
    Value(bool value) noexcept;
    Value(int value) noexcept;
    Value(std::int64_t value) noexcept;
    Value(double value) noexcept;
    Value(char const* value);
    Value(std::string value);

    [[nodiscard]] static Value array();
    [[nodiscard]] static Value object();

    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] bool is_bool() const noexcept;
    [[nodiscard]] bool is_number() const noexcept;
    [[nodiscard]] bool is_string() const noexcept;
    [[nodiscard]] bool is_array() const noexcept;
    [[nodiscard]] bool is_object() const noexcept;

    [[nodiscard]] std::optional<bool> as_bool() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> as_int() const noexcept;
    [[nodiscard]] std::optional<double> as_double() const noexcept;
    [[nodiscard]] std::optional<std::string_view> as_string() const noexcept;

    // array/object 원소 수를 반환한다. 그 외 타입은 0을 반환한다.
    [[nodiscard]] std::size_t size() const noexcept;

    // array 원소를 조회한다. array가 아니거나 범위를 벗어나면 nullptr를 반환한다.
    [[nodiscard]] Value const* at(std::size_t index) const noexcept;

    // array 끝에 원소를 추가한다. null이면 array로 승격한다. array/null이 아니면 false.
    [[nodiscard]] bool try_push_back(Value value);

    // try_push_back과 같지만 체이닝용이다. 대상은 array 또는 null이어야 한다.
    Value& push_back(Value value);

    // object 멤버를 조회한다. object가 아니거나 키가 없으면 nullptr를 반환한다.
    [[nodiscard]] Value const* find(std::string_view key) const noexcept;

    // object에 key가 있는지 확인한다.
    [[nodiscard]] bool contains(std::string_view key) const noexcept;

    // object 멤버를 삽입 순서대로 순회한다. object가 아니면 아무 일도 하지 않는다.
    template <typename Visitor>
        requires std::invocable<Visitor&, std::string_view, Value const&>
    void for_each_member(Visitor&& visitor) const {
        if (auto const* members = std::get_if<Object>(&data_)) {
            for (auto const& [key, value] : *members) {
                std::invoke(visitor, std::string_view{key}, value);
            }
        }
    }

    // null을 object로 승격하고, 기존 키는 위치를 유지한 채 갱신한다. object/null이 아니면 false.
    [[nodiscard]] bool try_set(std::string key, Value value);

    // try_set과 같지만 체이닝용이다. 대상은 object 또는 null이어야 한다.
    Value& set(std::string key, Value value);

    // 현재 값을 불필요한 공백 없이 compact JSON text로 직렬화해 반환한다.
    [[nodiscard]] std::string dump() const;

    bool operator==(Value const&) const = default;

private:
    // 현재 값을 compact JSON text로 out 뒤에 덧붙인다.
    void dump_to(std::string& out) const;

    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> data_;
};

// JSON text를 파싱한다. 실패하면 nullopt를 반환한다.
[[nodiscard]] std::optional<Value> parse(std::string_view text);

} // namespace ddcs::json
