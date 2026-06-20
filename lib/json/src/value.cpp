#include "ddcs/json/value.hpp"

#include "ddcs/json/writer.hpp"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace ddcs::json {

Value::Value() noexcept
    : data_(nullptr) {}

Value::Value(std::nullptr_t) noexcept
    : data_(nullptr) {}

Value::Value(bool value) noexcept
    : data_(value) {}

Value::Value(int value) noexcept
    : data_(static_cast<std::int64_t>(value)) {}

Value::Value(std::int64_t value) noexcept
    : data_(value) {}

Value::Value(double value) noexcept
    : data_(value) {}

Value::Value(char const* value)
    : data_(std::string{value}) {}

Value::Value(std::string value)
    : data_(std::move(value)) {}

Value Value::array() {
    Value value;
    value.data_ = Array{};
    return value;
}

Value Value::object() {
    Value value;
    value.data_ = Object{};
    return value;
}

bool Value::is_null() const noexcept {
    return std::holds_alternative<std::nullptr_t>(data_);
}

bool Value::is_bool() const noexcept {
    return std::holds_alternative<bool>(data_);
}

bool Value::is_number() const noexcept {
    return std::holds_alternative<std::int64_t>(data_) || std::holds_alternative<double>(data_);
}

bool Value::is_string() const noexcept {
    return std::holds_alternative<std::string>(data_);
}

bool Value::is_array() const noexcept {
    return std::holds_alternative<Array>(data_);
}

bool Value::is_object() const noexcept {
    return std::holds_alternative<Object>(data_);
}

std::optional<bool> Value::as_bool() const noexcept {
    if (auto const* value = std::get_if<bool>(&data_)) {
        return *value;
    }

    return std::nullopt;
}

std::optional<std::int64_t> Value::as_int64() const noexcept {
    if (auto const* value = std::get_if<std::int64_t>(&data_)) {
        return *value;
    }

    return std::nullopt;
}

std::optional<double> Value::as_double() const noexcept {
    if (auto const* value = std::get_if<double>(&data_)) {
        return *value;
    }

    if (auto const* value = std::get_if<std::int64_t>(&data_)) {
        return static_cast<double>(*value); // 정수도 double로 허용
    }

    return std::nullopt;
}

std::optional<std::string_view> Value::as_string() const noexcept {
    if (auto const* value = std::get_if<std::string>(&data_)) {
        return std::string_view{*value};
    }

    return std::nullopt;
}

std::size_t Value::size() const noexcept {
    if (auto const* elements = std::get_if<Array>(&data_)) {
        return elements->size();
    }

    if (auto const* members = std::get_if<Object>(&data_)) {
        return members->size();
    }

    return 0;
}

Value const* Value::at(std::size_t index) const noexcept {
    auto const* elements = std::get_if<Array>(&data_);
    if (elements == nullptr || index >= elements->size()) {
        return nullptr;
    }

    return &(*elements)[index];
}

bool Value::try_push_back(Value value) {
    if (is_null()) {
        data_ = Array{};
    }

    auto* elements = std::get_if<Array>(&data_);
    if (elements == nullptr) {
        return false;
    }

    elements->push_back(std::move(value));
    return true;
}

Value& Value::push_back(Value value) {
    if (!try_push_back(std::move(value))) {
        assert(false && "Value: push_back target must be array or null");
        throw std::logic_error{"Value: push_back target must be array or null"};
    }

    return *this;
}

Value const* Value::find(std::string_view key) const noexcept {
    auto const* members = std::get_if<Object>(&data_);
    if (members == nullptr) {
        return nullptr;
    }

    for (auto const& [member_key, member_value] : *members) {
        if (member_key == key) {
            return &member_value;
        }
    }

    return nullptr;
}

bool Value::contains(std::string_view key) const noexcept {
    return find(key) != nullptr;
}

bool Value::try_set(std::string key, Value value) {
    if (is_null()) {
        data_ = Object{};
    }

    auto* members = std::get_if<Object>(&data_);
    if (members == nullptr) {
        return false;
    }

    // 기존 키는 삽입 위치를 유지한 채 값만 갱신한다.
    for (auto& [member_key, member_value] : *members) {
        if (member_key == key) {
            member_value = std::move(value);
            return true;
        }
    }

    members->emplace_back(std::move(key), std::move(value));
    return true;
}

Value& Value::set(std::string key, Value value) {
    if (!try_set(std::move(key), std::move(value))) {
        assert(false && "Value: set target must be object or null");
        throw std::logic_error{"Value: set target must be object or null"};
    }

    return *this;
}

std::string Value::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

void Value::dump_to(std::string& out) const {
    if (std::holds_alternative<std::nullptr_t>(data_)) {
        append_null(out);
        return;
    }

    if (auto const* bool_value = std::get_if<bool>(&data_)) {
        append_bool(out, *bool_value);
        return;
    }

    if (auto const* int_value = std::get_if<std::int64_t>(&data_)) {
        append_number(out, *int_value);
        return;
    }

    if (auto const* double_value = std::get_if<double>(&data_)) {
        append_number(out, *double_value);
        return;
    }

    if (auto const* string_value = std::get_if<std::string>(&data_)) {
        append_string_literal(out, *string_value);
        return;
    }

    if (auto const* elements = std::get_if<Array>(&data_)) {
        out += '[';
        for (std::size_t index = 0; index < elements->size(); ++index) {
            if (index != 0) {
                out += ',';
            }
            (*elements)[index].dump_to(out);
        }
        out += ']';
        return;
    }

    auto const& members = std::get<Object>(data_);

    out += '{';
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (index != 0) {
            out += ',';
        }

        auto const& [key, value] = members[index];
        append_string_literal(out, key);
        out += ':';
        value.dump_to(out);
    }

    out += '}';
}

} // namespace ddcs::json
