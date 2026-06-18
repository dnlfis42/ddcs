#include "ddcs/json/value.hpp"

#include <array>
#include <cassert>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ddcs::json {

Value::Value() noexcept
    : data_{nullptr} {}

Value::Value(std::nullptr_t) noexcept
    : data_{nullptr} {}

Value::Value(bool value) noexcept
    : data_{value} {}

Value::Value(int value) noexcept
    : data_{static_cast<std::int64_t>(value)} {}

Value::Value(std::int64_t value) noexcept
    : data_{value} {}

Value::Value(double value) noexcept
    : data_{value} {}

Value::Value(char const* value)
    : data_{std::string{value}} {}

Value::Value(std::string value)
    : data_{std::move(value)} {}

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

std::optional<std::int64_t> Value::as_int() const noexcept {
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
        assert(false && "Value::push_back target must be array or null");
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
        assert(false && "Value::set target must be object or null");
        throw std::logic_error{"Value: set target must be object or null"};
    }

    return *this;
}

namespace {

// UTF-16 code unit을 JSON Unicode escape(\uXXXX)로 덧붙인다.
void append_json_unicode_escape(std::string& out, std::uint16_t utf16_code_unit) {
    constexpr char digits[] = "0123456789abcdef";

    out += "\\u";
    out += digits[(utf16_code_unit >> 12) & 0xF];
    out += digits[(utf16_code_unit >> 8) & 0xF];
    out += digits[(utf16_code_unit >> 4) & 0xF];
    out += digits[utf16_code_unit & 0xF];
}

// double 값을 JSON 값으로 덧붙인다. NaN/Infinity는 JSON에 없으므로 null로 출력한다.
void append_json_number(std::string& out, double value) {
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }

    std::array<char, 32> buf{};
    // 다시 파싱해도 같은 double이 되는 가장 짧은 표현으로 쓴다.
    auto const res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
    out.append(buf.data(), res.ptr);
}

// 문자열 값을 JSON string literal로 덧붙인다.
// 제어문자는 escape하고, 유효한 UTF-8 바이트열은 그대로 둔다.
void append_json_string_literal(std::string& out, std::string_view value) {
    out += '"';
    for (char ch : value) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                append_json_unicode_escape(
                    out, static_cast<std::uint16_t>(static_cast<unsigned char>(ch))
                );
            } else {
                out += ch;
            }
        }
    }

    out += '"';
}

} // namespace

std::string Value::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

void Value::dump_to(std::string& out) const {
    if (std::holds_alternative<std::nullptr_t>(data_)) {
        out += "null";
        return;
    }

    if (auto const* bool_value = std::get_if<bool>(&data_)) {
        out += *bool_value ? "true" : "false";
        return;
    }

    if (auto const* int_value = std::get_if<std::int64_t>(&data_)) {
        out += std::to_string(*int_value);
        return;
    }

    if (auto const* double_value = std::get_if<double>(&data_)) {
        append_json_number(out, *double_value);
        return;
    }

    if (auto const* string_value = std::get_if<std::string>(&data_)) {
        append_json_string_literal(out, *string_value);
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
        append_json_string_literal(out, key);
        out += ':';
        value.dump_to(out);
    }

    out += '}';
}

namespace {

namespace utf8 {

void append_scalar(std::string& out, std::uint32_t scalar_value) {
    if (scalar_value <= 0x7F) {
        out += static_cast<char>(scalar_value);
    } else if (scalar_value <= 0x7FF) {
        out += static_cast<char>(0xC0 | (scalar_value >> 6));
        out += static_cast<char>(0x80 | (scalar_value & 0x3F));
    } else if (scalar_value <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (scalar_value >> 12));
        out += static_cast<char>(0x80 | ((scalar_value >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (scalar_value & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (scalar_value >> 18));
        out += static_cast<char>(0x80 | ((scalar_value >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((scalar_value >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (scalar_value & 0x3F));
    }
}

} // namespace utf8

namespace utf16 {

[[nodiscard]] constexpr bool is_high_surrogate(std::uint16_t code_unit) noexcept {
    return code_unit >= 0xD800 && code_unit <= 0xDBFF;
}

[[nodiscard]] constexpr bool is_low_surrogate(std::uint16_t code_unit) noexcept {
    return code_unit >= 0xDC00 && code_unit <= 0xDFFF;
}

[[nodiscard]] constexpr std::uint32_t
decode_surrogate_pair(std::uint16_t high_surrogate, std::uint16_t low_surrogate) noexcept {
    return 0x10000u + ((high_surrogate - 0xD800u) << 10) + (low_surrogate - 0xDC00u);
}

} // namespace utf16

[[nodiscard]] constexpr bool is_digit(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

[[nodiscard]] constexpr bool is_digit_1_to_9(char ch) noexcept {
    return ch >= '1' && ch <= '9';
}

class Parser {
public:
    explicit Parser(std::string_view text) noexcept
        : text_{text} {}

    // 입력 전체가 하나의 JSON value여야 한다. 뒤에 공백 외 문자가 남으면 실패한다.
    [[nodiscard]] std::optional<Value> parse() {
        auto value = parse_value();
        if (!value) {
            return std::nullopt;
        }

        skip_ws();
        if (pos_ != text_.size()) {
            return std::nullopt;
        }

        return value;
    }

private:
    static constexpr int max_depth = 64;

    // cursor primitives

    [[nodiscard]] bool eof() const noexcept {
        return pos_ >= text_.size();
    }

    [[nodiscard]] char current() const noexcept {
        return text_[pos_];
    }

    char consume() noexcept {
        return text_[pos_++];
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return text_.size() - pos_;
    }

    // lexical helpers

    void skip_ws() noexcept {
        while (!eof()) {
            char const ch = current();
            if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
                consume();
            } else {
                break;
            }
        }
    }

    bool try_consume_literal(std::string_view literal) {
        if (remaining() >= literal.size() && text_.substr(pos_, literal.size()) == literal) {
            pos_ += literal.size();
            return true;
        }

        return false;
    }

    // JSON \uXXXX escape의 4자리 hex를 UTF-16 code unit으로 읽는다.
    std::optional<std::uint16_t> parse_hex4_code_unit() {
        if (remaining() < 4) {
            return std::nullopt;
        }

        std::uint16_t code_unit{0};

        for (int i = 0; i < 4; ++i) {
            char const ch = consume();
            code_unit = static_cast<std::uint16_t>(code_unit << 4);

            if (ch >= '0' && ch <= '9') {
                code_unit |= static_cast<std::uint16_t>(ch - '0');
            } else if (ch >= 'a' && ch <= 'f') {
                code_unit |= static_cast<std::uint16_t>(ch - 'a' + 10);
            } else if (ch >= 'A' && ch <= 'F') {
                code_unit |= static_cast<std::uint16_t>(ch - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }

        return code_unit;
    }

    // JSON \uXXXX 또는 surrogate pair escape를 Unicode scalar value로 읽는다.
    std::optional<std::uint32_t> parse_unicode_escape_scalar() {
        auto code_unit = parse_hex4_code_unit();
        if (!code_unit) {
            return std::nullopt;
        }

        if (utf16::is_high_surrogate(*code_unit)) {
            if (remaining() < 2 || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                return std::nullopt;
            }

            pos_ += 2;

            auto low_surrogate = parse_hex4_code_unit();
            if (!low_surrogate || !utf16::is_low_surrogate(*low_surrogate)) {
                return std::nullopt;
            }

            return utf16::decode_surrogate_pair(*code_unit, *low_surrogate);
        }

        if (utf16::is_low_surrogate(*code_unit)) {
            return std::nullopt;
        }

        return *code_unit;
    }

    // grammar parsers

    std::optional<Value> parse_value() {
        skip_ws();

        if (eof()) {
            return std::nullopt;
        }

        switch (current()) {
        case 'n':
            if (!try_consume_literal("null")) {
                return std::nullopt;
            }
            return Value{nullptr};
        case 't':
            if (!try_consume_literal("true")) {
                return std::nullopt;
            }
            return Value{true};
        case 'f':
            if (!try_consume_literal("false")) {
                return std::nullopt;
            }
            return Value{false};
        case '"': {
            auto value = parse_string();
            if (!value) {
                return std::nullopt;
            }
            return Value{std::move(*value)};
        }
        case '[':
            return parse_array();
        case '{':
            return parse_object();
        default:
            if (current() == '-' || (current() >= '0' && current() <= '9')) {
                return parse_number();
            }

            return std::nullopt;
        }
    }

    std::optional<std::string> parse_string() {
        // 호출자는 현재 문자가 큰따옴표임을 보장한다.
        consume();

        std::string out;

        while (!eof()) {
            char const ch = consume();

            if (ch == '"') {
                return out;
            }

            if (ch == '\\') {
                if (!append_escape(out)) {
                    return std::nullopt;
                }
            } else if (static_cast<unsigned char>(ch) < 0x20) {
                return std::nullopt;
            } else {
                out += ch;
            }
        }

        return std::nullopt;
    }

    bool append_escape(std::string& out) {
        if (eof()) {
            return false;
        }

        char const escape = consume();

        switch (escape) {
        case '"':
            out += '"';
            return true;
        case '\\':
            out += '\\';
            return true;
        case '/':
            out += '/';
            return true;
        case 'b':
            out += '\b';
            return true;
        case 'f':
            out += '\f';
            return true;
        case 'n':
            out += '\n';
            return true;
        case 'r':
            out += '\r';
            return true;
        case 't':
            out += '\t';
            return true;
        case 'u': {
            auto scalar_value = parse_unicode_escape_scalar();
            if (!scalar_value) {
                return false;
            }

            utf8::append_scalar(out, *scalar_value);
            return true;
        }
        default:
            return false;
        }
    }

    std::optional<Value> parse_number() {
        std::size_t const begin = pos_;

        if (current() == '-') {
            consume();
            if (eof()) {
                return std::nullopt;
            }
        }

        if (current() == '0') {
            consume();
        } else if (is_digit_1_to_9(current())) {
            do {
                consume();
            } while (!eof() && is_digit(current()));
        } else {
            return std::nullopt;
        }

        bool is_double = false;
        if (!eof() && current() == '.') {
            is_double = true;
            consume();
            if (eof() || !is_digit(current())) {
                return std::nullopt;
            }

            do {
                consume();
            } while (!eof() && is_digit(current()));
        }

        if (!eof() && (current() == 'e' || current() == 'E')) {
            is_double = true;
            consume();
            if (!eof() && (current() == '+' || current() == '-')) {
                consume();
            }
            if (eof() || !is_digit(current())) {
                return std::nullopt;
            }

            do {
                consume();
            } while (!eof() && is_digit(current()));
        }

        char const* const first = text_.data() + begin;
        char const* const last = text_.data() + pos_;

        if (!is_double) {
            std::int64_t int_value{};
            auto const int_result = std::from_chars(first, last, int_value);
            if (int_result.ec == std::errc{} && int_result.ptr == last) {
                return Value{int_value};
            }
        }

        double double_value{};
        auto const double_result = std::from_chars(first, last, double_value);

        if (double_result.ec != std::errc{} || double_result.ptr != last) {
            return std::nullopt;
        }

        return Value{double_value};
    }

    std::optional<Value> parse_array() {
        if (++depth_ > max_depth) {
            return std::nullopt;
        }

        consume();

        Value array = Value::array();

        skip_ws();
        if (!eof() && current() == ']') {
            consume();
            --depth_;
            return array;
        }

        for (;;) {
            auto value = parse_value();
            if (!value) {
                --depth_;
                return std::nullopt;
            }

            array.push_back(std::move(*value));

            skip_ws();
            if (eof()) {
                --depth_;
                return std::nullopt;
            }

            char const ch = consume();
            if (ch == ']') {
                --depth_;
                return array;
            }

            if (ch != ',') {
                --depth_;
                return std::nullopt;
            }

            skip_ws();
        }
    }

    std::optional<Value> parse_object() {
        if (++depth_ > max_depth) {
            return std::nullopt;
        }

        consume();

        Value object = Value::object();

        skip_ws();
        if (!eof() && current() == '}') {
            consume();
            --depth_;
            return object;
        }

        for (;;) {
            skip_ws();

            if (eof() || current() != '"') {
                --depth_;
                return std::nullopt;
            }

            auto key = parse_string();
            if (!key) {
                --depth_;
                return std::nullopt;
            }

            skip_ws();
            if (eof() || current() != ':') {
                --depth_;
                return std::nullopt;
            }

            consume();

            auto value = parse_value();
            if (!value) {
                --depth_;
                return std::nullopt;
            }

            object.set(std::move(*key), std::move(*value));

            skip_ws();
            if (eof()) {
                --depth_;
                return std::nullopt;
            }

            char const ch = consume();
            if (ch == '}') {
                --depth_;
                return object;
            }

            if (ch != ',') {
                --depth_;
                return std::nullopt;
            }
        }
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    int depth_ = 0;
};

} // namespace

std::optional<Value> parse(std::string_view text) {
    return Parser{text}.parse();
}

} // namespace ddcs::json
