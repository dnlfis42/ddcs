#include "ddcs/logger/log.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>

namespace ddcs::logger {

namespace {

constexpr std::string_view level_name(Level lvl) noexcept {
    switch (lvl) {
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Warn:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

// 파일 경로에서 basename(마지막 '/' 이후) 추출 - 컴파일타임 const char* 입력.
constexpr std::string_view basename_of(char const* path) noexcept {
    if (path == nullptr) {
        return {};
    }
    std::string_view sv{path};
    auto pos = sv.find_last_of('/');
    return pos == std::string_view::npos ? sv : sv.substr(pos + 1);
}

// ISO8601 UTC + ms 정밀도. e.g. "2026-05-28T12:34:56.789Z"
void append_iso8601(std::string& buf) {
    auto const now = std::chrono::system_clock::now();
    auto const sec = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto const ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    auto const t = std::chrono::system_clock::to_time_t(sec);

    std::tm tm{};
    ::gmtime_r(&t, &tm);

    std::array<char, 32> tmp{};
    int const n = std::snprintf(
        tmp.data(), tmp.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms)
    );
    if (n > 0) {
        buf.append(tmp.data(), static_cast<std::size_t>(n));
    }
}

// JSON 문자열 escape 가 필요한 byte 인지.
constexpr bool needs_escape(unsigned char c) noexcept { return c == '"' || c == '\\' || c < 0x20; }

void append_json_escaped(std::string& buf, std::string_view s) {
    // fast path: escape 대상 없으면 한 번에 memcpy.
    auto const first =
        std::find_if(s.begin(), s.end(), [](char c) { return needs_escape(static_cast<unsigned char>(c)); });
    if (first == s.end()) {
        buf.append(s.data(), s.size());
        return;
    }
    // 일치 전까지는 통째로 복사.
    buf.append(s.data(), static_cast<std::size_t>(first - s.begin()));
    for (auto it = first; it != s.end(); ++it) {
        unsigned char const c = static_cast<unsigned char>(*it);
        switch (c) {
        case '"':
            buf.append("\\\"", 2);
            break;
        case '\\':
            buf.append("\\\\", 2);
            break;
        case '\n':
            buf.append("\\n", 2);
            break;
        case '\r':
            buf.append("\\r", 2);
            break;
        case '\t':
            buf.append("\\t", 2);
            break;
        case '\b':
            buf.append("\\b", 2);
            break;
        case '\f':
            buf.append("\\f", 2);
            break;
        default:
            if (c < 0x20) {
                std::array<char, 8> tmp{};
                int const n = std::snprintf(tmp.data(), tmp.size(), "\\u%04x", static_cast<unsigned>(c));
                if (n > 0) {
                    buf.append(tmp.data(), static_cast<std::size_t>(n));
                }
            } else {
                buf.push_back(static_cast<char>(c));
            }
            break;
        }
    }
}

template <typename T>
void append_int(std::string& buf, T v) {
    std::array<char, 32> tmp{};
    auto const r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), v);
    if (r.ec == std::errc{}) {
        buf.append(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data()));
    }
}

} // namespace

Level level_from_string(std::string_view name, Level fallback) noexcept {
    auto const ieq = [](std::string_view a, std::string_view lower) noexcept {
        if (a.size() != lower.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) != static_cast<unsigned char>(lower[i])) {
                return false;
            }
        }
        return true;
    };
    if (ieq(name, "debug")) {
        return Level::Debug;
    }
    if (ieq(name, "info")) {
        return Level::Info;
    }
    if (ieq(name, "warn") || ieq(name, "warning")) {
        return Level::Warn;
    }
    if (ieq(name, "error")) {
        return Level::Error;
    }
    return fallback;
}

namespace detail {

void append_value(std::string& buf, std::string_view v) {
    buf.push_back('"');
    append_json_escaped(buf, v);
    buf.push_back('"');
}

void append_value(std::string& buf, bool v) { buf.append(v ? "true" : "false"); }

void append_value(std::string& buf, std::nullptr_t) { buf.append("null", 4); }

void append_value(std::string& buf, double v) {
    std::array<char, 32> tmp{};
    auto const r = std::to_chars(tmp.data(), tmp.data() + tmp.size(), v);
    if (r.ec == std::errc{}) {
        buf.append(tmp.data(), static_cast<std::size_t>(r.ptr - tmp.data()));
    } else {
        buf.append("null", 4); // NaN/Inf 등은 JSON 표준에 없으므로 null.
    }
}

void append_value(std::string& buf, std::int64_t v) { append_int(buf, v); }
void append_value(std::string& buf, std::uint64_t v) { append_int(buf, v); }

void build_header(std::string& buf, Level lvl, std::source_location const& loc, std::string_view msg) {
    buf.append("{\"ts\":\"", 7);
    append_iso8601(buf);
    buf.append("\",\"level\":\"", 11);
    buf.append(level_name(lvl));
    buf.append("\",\"file\":\"", 10);
    append_json_escaped(buf, basename_of(loc.file_name()));
    buf.append("\",\"line\":", 9);
    append_int(buf, static_cast<std::uint32_t>(loc.line()));
    buf.append(",\"msg\":\"", 8);
    append_json_escaped(buf, msg);
    buf.push_back('"');
}

void finish_line(std::string& buf) { buf.push_back('}'); }

std::string& thread_buffer() noexcept {
    thread_local std::string buf;
    if (buf.capacity() < 256) {
        buf.reserve(256);
    }
    return buf;
}

} // namespace detail

void StdoutSink::write(std::string_view line) noexcept {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    // 컨테이너 stdout 은 fully-buffered 라 명시 flush 필요. 가시성 우선.
    std::fflush(stdout);
}

void StdoutSink::flush() noexcept { std::fflush(stdout); }

Logger& Logger::instance() noexcept {
    static Logger inst;
    return inst;
}

} // namespace ddcs::logger
