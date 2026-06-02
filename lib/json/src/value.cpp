#include "ddcs/json/value.hpp"

#include <array>
#include <charconv>
#include <string>
#include <string_view>
#include <utility>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ddcs::json {

// --- ctor -------------------------------------------------------------------
Value::Value() noexcept : data_{nullptr} {}
Value::Value(std::nullptr_t) noexcept : data_{nullptr} {}
Value::Value(bool b) noexcept : data_{b} {}
Value::Value(int n) noexcept : data_{static_cast<std::int64_t>(n)} {}
Value::Value(std::int64_t n) noexcept : data_{n} {}
Value::Value(std::uint64_t n) noexcept : data_{static_cast<std::int64_t>(n)} {}
Value::Value(double d) noexcept : data_{d} {}
Value::Value(char const* s) : data_{std::string{s}} {}
Value::Value(std::string s) : data_{std::move(s)} {}

Value Value::object() {
    Value v;
    v.data_ = Object{};
    return v;
}
Value Value::array() {
    Value v;
    v.data_ = Array{};
    return v;
}

// --- 타입 질의 ---------------------------------------------------------------
bool Value::is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(data_); }
bool Value::is_bool() const noexcept { return std::holds_alternative<bool>(data_); }
bool Value::is_number() const noexcept {
    return std::holds_alternative<std::int64_t>(data_) || std::holds_alternative<double>(data_);
}
bool Value::is_string() const noexcept { return std::holds_alternative<std::string>(data_); }
bool Value::is_array() const noexcept { return std::holds_alternative<Array>(data_); }
bool Value::is_object() const noexcept { return std::holds_alternative<Object>(data_); }

// --- 접근 --------------------------------------------------------------------
std::optional<bool> Value::as_bool() const noexcept {
    if (auto const* p = std::get_if<bool>(&data_)) {
        return *p;
    }
    return std::nullopt;
}
std::optional<std::int64_t> Value::as_int() const noexcept {
    if (auto const* p = std::get_if<std::int64_t>(&data_)) {
        return *p;
    }
    return std::nullopt;
}
std::optional<double> Value::as_double() const noexcept {
    if (auto const* p = std::get_if<double>(&data_)) {
        return *p;
    }
    if (auto const* p = std::get_if<std::int64_t>(&data_)) {
        return static_cast<double>(*p); // 정수도 double 로 허용
    }
    return std::nullopt;
}
std::optional<std::string_view> Value::as_string() const noexcept {
    if (auto const* p = std::get_if<std::string>(&data_)) {
        return std::string_view{*p};
    }
    return std::nullopt;
}

// --- object / array 연산 -----------------------------------------------------
Value& Value::set(std::string key, Value v) {
    if (is_null()) {
        data_ = Object{}; // null -> object 승격
    }
    auto& obj = std::get<Object>(data_);
    for (auto& [k, existing] : obj) {
        if (k == key) {
            existing = std::move(v); // 위치 유지 갱신
            return *this;
        }
    }
    obj.emplace_back(std::move(key), std::move(v));
    return *this;
}

Value const* Value::find(std::string_view key) const noexcept {
    auto const* obj = std::get_if<Object>(&data_);
    if (obj == nullptr) {
        return nullptr;
    }
    for (auto const& [k, v] : *obj) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

bool Value::contains(std::string_view key) const noexcept { return find(key) != nullptr; }

Value& Value::push_back(Value v) {
    if (is_null()) {
        data_ = Array{}; // null -> array 승격
    }
    std::get<Array>(data_).push_back(std::move(v));
    return *this;
}

std::size_t Value::size() const noexcept {
    if (auto const* a = std::get_if<Array>(&data_)) {
        return a->size();
    }
    if (auto const* o = std::get_if<Object>(&data_)) {
        return o->size();
    }
    return 0;
}

Value const* Value::at(std::size_t i) const noexcept {
    auto const* a = std::get_if<Array>(&data_);
    if (a == nullptr || i >= a->size()) {
        return nullptr;
    }
    return &(*a)[i];
}

// --- dump (직렬화) -----------------------------------------------------------
namespace {

void append_hex4(std::string& out, unsigned cp) {
    constexpr char digits[] = "0123456789abcdef";
    out += "\\u";
    out += digits[(cp >> 12) & 0xF];
    out += digits[(cp >> 8) & 0xF];
    out += digits[(cp >> 4) & 0xF];
    out += digits[cp & 0xF];
}

void dump_string(std::string& out, std::string_view s) {
    out += '"';
    for (char c : s) {
        switch (c) {
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
            if (static_cast<unsigned char>(c) < 0x20) {
                append_hex4(out, static_cast<unsigned char>(c)); // 제어문자 -> \u00XX
            } else {
                out += c; // UTF-8 바이트는 그대로 통과(유효 JSON)
            }
        }
    }
    out += '"';
}

void dump_double(std::string& out, double d) {
    if (!std::isfinite(d)) {
        out += "null"; // NaN/Inf 는 JSON 비표준 -> null 로 방어
        return;
    }
    std::array<char, 32> buf{};
    auto const res = std::to_chars(buf.data(), buf.data() + buf.size(), d);
    out.append(buf.data(), res.ptr); // 최단 왕복 표현
}

} // namespace

void Value::dump_to(std::string& out) const {
    if (std::holds_alternative<std::nullptr_t>(data_)) {
        out += "null";
        return;
    }
    if (auto const* b = std::get_if<bool>(&data_)) {
        out += *b ? "true" : "false";
        return;
    }
    if (auto const* n = std::get_if<std::int64_t>(&data_)) {
        out += std::to_string(*n);
        return;
    }
    if (auto const* d = std::get_if<double>(&data_)) {
        dump_double(out, *d);
        return;
    }
    if (auto const* s = std::get_if<std::string>(&data_)) {
        dump_string(out, *s);
        return;
    }
    if (auto const* a = std::get_if<Array>(&data_)) {
        out += '[';
        for (std::size_t i = 0; i < a->size(); ++i) {
            if (i != 0) {
                out += ',';
            }
            (*a)[i].dump_to(out);
        }
        out += ']';
        return;
    }
    auto const& obj = std::get<Object>(data_);
    out += '{';
    for (std::size_t i = 0; i < obj.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        dump_string(out, obj[i].first);
        out += ':';
        obj[i].second.dump_to(out);
    }
    out += '}';
}

std::string Value::dump() const {
    std::string out;
    dump_to(out);
    return out;
}

// --- parse (역직렬화) --------------------------------------------------------
namespace {

constexpr int max_depth{64}; // 재귀 깊이 상한 - 악의적/사고성 깊은 중첩 방어

struct Parser {
    std::string_view s;
    std::size_t pos{0};
    int depth{0};

    bool eof() const noexcept { return pos >= s.size(); }
    char cur() const noexcept { return s[pos]; }

    void skip_ws() noexcept {
        while (!eof()) {
            char const c = s[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
            } else {
                break;
            }
        }
    }

    // 4 hex -> 코드포인트. 실패 시 nullopt.
    std::optional<unsigned> parse_hex4() {
        if (s.size() - pos < 4) {
            return std::nullopt;
        }
        unsigned cp{0};
        for (int i = 0; i < 4; ++i) {
            char const c = s[pos++];
            cp <<= 4;
            if (c >= '0' && c <= '9') {
                cp |= static_cast<unsigned>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                cp |= static_cast<unsigned>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                cp |= static_cast<unsigned>(c - 'A' + 10);
            } else {
                return std::nullopt;
            }
        }
        return cp;
    }

    static void encode_utf8(std::string& out, unsigned cp) {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::optional<std::string> parse_string() {
        ++pos; // 여는 따옴표
        std::string out;
        while (!eof()) {
            char const c = s[pos++];
            if (c == '"') {
                return out;
            }
            if (c == '\\') {
                if (eof()) {
                    return std::nullopt;
                }
                char const e = s[pos++];
                switch (e) {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    auto cp = parse_hex4();
                    if (!cp) {
                        return std::nullopt;
                    }
                    if (*cp >= 0xD800 && *cp <= 0xDBFF) { // high surrogate -> low 필요
                        if (s.size() - pos < 2 || s[pos] != '\\' || s[pos + 1] != 'u') {
                            return std::nullopt;
                        }
                        pos += 2;
                        auto lo = parse_hex4();
                        if (!lo || *lo < 0xDC00 || *lo > 0xDFFF) {
                            return std::nullopt;
                        }
                        unsigned const combined = 0x10000u + ((*cp - 0xD800u) << 10) + (*lo - 0xDC00u);
                        encode_utf8(out, combined);
                    } else if (*cp >= 0xDC00 && *cp <= 0xDFFF) {
                        return std::nullopt; // 외톨이 low surrogate
                    } else {
                        encode_utf8(out, *cp);
                    }
                    break;
                }
                default:
                    return std::nullopt; // 미지 escape
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                return std::nullopt; // 비이스케이프 제어문자 -> strict reject
            } else {
                out += c;
            }
        }
        return std::nullopt; // 닫는 따옴표 없이 eof
    }

    std::optional<Value> parse_number() {
        std::size_t const begin = pos;
        char const* const first = s.data() + pos;
        char const* const last = s.data() + s.size();

        // 정수 우선 시도. 뒤에 '.', 'e', 'E' 가 붙으면 실수로 재해석.
        std::int64_t ival{};
        auto const ir = std::from_chars(first, last, ival);
        if (ir.ec == std::errc{} && ir.ptr != first) {
            bool const is_float = ir.ptr != last && (*ir.ptr == '.' || *ir.ptr == 'e' || *ir.ptr == 'E');
            if (!is_float) {
                pos = begin + static_cast<std::size_t>(ir.ptr - first);
                return Value{ival};
            }
        }
        double dval{};
        auto const dr = std::from_chars(first, last, dval);
        if (dr.ec != std::errc{} || dr.ptr == first) {
            return std::nullopt;
        }
        pos = begin + static_cast<std::size_t>(dr.ptr - first);
        return Value{dval};
    }

    bool consume_literal(std::string_view lit) {
        if (s.size() - pos >= lit.size() && s.substr(pos, lit.size()) == lit) {
            pos += lit.size();
            return true;
        }
        return false;
    }

    std::optional<Value> parse_array() {
        if (++depth > max_depth) {
            return std::nullopt;
        }
        ++pos; // '['
        Value arr = Value::array();
        skip_ws();
        if (!eof() && cur() == ']') {
            ++pos;
            --depth;
            return arr;
        }
        for (;;) {
            auto v = parse_value();
            if (!v) {
                return std::nullopt;
            }
            arr.push_back(std::move(*v));
            skip_ws();
            if (eof()) {
                return std::nullopt;
            }
            char const c = s[pos++];
            if (c == ']') {
                break;
            }
            if (c != ',') {
                return std::nullopt;
            }
            skip_ws();
        }
        --depth;
        return arr;
    }

    std::optional<Value> parse_object() {
        if (++depth > max_depth) {
            return std::nullopt;
        }
        ++pos; // '{'
        Value obj = Value::object();
        skip_ws();
        if (!eof() && cur() == '}') {
            ++pos;
            --depth;
            return obj;
        }
        for (;;) {
            skip_ws();
            if (eof() || cur() != '"') {
                return std::nullopt; // 키는 문자열
            }
            auto key = parse_string();
            if (!key) {
                return std::nullopt;
            }
            skip_ws();
            if (eof() || s[pos++] != ':') {
                return std::nullopt;
            }
            auto v = parse_value();
            if (!v) {
                return std::nullopt;
            }
            obj.set(std::move(*key), std::move(*v));
            skip_ws();
            if (eof()) {
                return std::nullopt;
            }
            char const c = s[pos++];
            if (c == '}') {
                break;
            }
            if (c != ',') {
                return std::nullopt;
            }
        }
        --depth;
        return obj;
    }

    std::optional<Value> parse_value() {
        skip_ws();
        if (eof()) {
            return std::nullopt;
        }
        switch (cur()) {
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case '"': {
            auto str = parse_string();
            if (!str) {
                return std::nullopt;
            }
            return Value{std::move(*str)};
        }
        case 't':
            return consume_literal("true") ? std::optional<Value>{Value{true}} : std::nullopt;
        case 'f':
            return consume_literal("false") ? std::optional<Value>{Value{false}} : std::nullopt;
        case 'n':
            return consume_literal("null") ? std::optional<Value>{Value{nullptr}} : std::nullopt;
        default:
            if (cur() == '-' || (cur() >= '0' && cur() <= '9')) {
                return parse_number();
            }
            return std::nullopt;
        }
    }
};

} // namespace

std::optional<Value> Value::parse(std::string_view in) {
    Parser p{in};
    auto v = p.parse_value();
    if (!v) {
        return std::nullopt;
    }
    p.skip_ws();
    if (p.pos != p.s.size()) {
        return std::nullopt; // trailing garbage
    }
    return v;
}

} // namespace ddcs::json
