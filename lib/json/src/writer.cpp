#include "ddcs/json/writer.hpp"

#include <array>
#include <charconv>
#include <cmath>

namespace ddcs::json {

namespace {

void append_json_unicode_escape(std::string& out, std::uint16_t utf16_code_unit) {
    constexpr char digits[] = "0123456789abcdef";

    out += "\\u";
    out += digits[(utf16_code_unit >> 12) & 0xF];
    out += digits[(utf16_code_unit >> 8) & 0xF];
    out += digits[(utf16_code_unit >> 4) & 0xF];
    out += digits[utf16_code_unit & 0xF];
}

} // namespace

void append_null(std::string& out) {
    out += "null";
}

void append_bool(std::string& out, bool value) {
    out += value ? "true" : "false";
}

void append_number(std::string& out, std::int64_t value) {
    std::array<char, 32> buf{};
    auto const res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
    if (res.ec == std::errc{}) {
        out.append(buf.data(), res.ptr);
    }
}

void append_number(std::string& out, double value) {
    if (!std::isfinite(value)) {
        append_null(out);
        return;
    }

    std::array<char, 32> buf{};
    auto const res = std::to_chars(buf.data(), buf.data() + buf.size(), value);
    if (res.ec == std::errc{}) {
        out.append(buf.data(), res.ptr);
    } else {
        append_null(out);
    }
}

void append_string_literal(std::string& out, std::string_view value) {
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

} // namespace ddcs::json
