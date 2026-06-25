#include "ddcs/logger/log.hpp"

#include "ddcs/json/writer.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace ddcs::logger {

namespace {

// 파일 경로에서 basename(마지막 '/' 이후)을 추출한다.
constexpr std::string_view basename_of(char const* path) noexcept {
    if (path == nullptr) {
        return {};
    }

    std::string_view sv{path};
    auto pos = sv.find_last_of('/');
    return pos == std::string_view::npos ? sv : sv.substr(pos + 1);
}

// ISO8601 UTC + ms 정밀도 (e.g. "2026-05-28T12:34:56.789Z")
void append_iso8601_utc(std::string& out) {
    auto const now = std::chrono::system_clock::now();
    auto const sec = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto const ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
        1000;
    auto const t = std::chrono::system_clock::to_time_t(sec);

    std::tm tm{};
    ::gmtime_r(&t, &tm);

    std::array<char, 32> tmp{};
    int const n = std::snprintf(
        tmp.data(), tmp.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900,
        tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms)
    );
    if (n > 0) {
        out.append(tmp.data(), static_cast<std::size_t>(n));
    }
}

template <typename T>
void append_decimal(std::string& out, T value) {
    std::array<char, 32> tmp{};
    auto const result = std::to_chars(tmp.data(), tmp.data() + tmp.size(), value);
    if (result.ec == std::errc{}) {
        out.append(tmp.data(), static_cast<std::size_t>(result.ptr - tmp.data()));
    }
}

} // namespace

std::optional<Level> parse_level(std::string_view text) noexcept {
    auto const ieq = [](std::string_view a, std::string_view lower) noexcept {
        if (a.size() != lower.size()) {
            return false;
        }

        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                static_cast<unsigned char>(lower[i])) {
                return false;
            }
        }
        return true;
    };

    if (ieq(text, "debug")) {
        return Level::debug;
    }
    if (ieq(text, "info")) {
        return Level::info;
    }
    if (ieq(text, "warn") || ieq(text, "warning")) {
        return Level::warn;
    }
    if (ieq(text, "error")) {
        return Level::error;
    }
    return std::nullopt;
}

void StdoutSink::write(std::string_view line) noexcept {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    // 컨테이너 stdout은 fully-buffered라 명시 flush가 필요하다.
    // 처리량보다 가시성을 우선한다.
    std::fflush(stdout);
}

void StdoutSink::flush() noexcept {
    std::fflush(stdout);
}

Logger& Logger::instance() noexcept {
    static Logger inst;
    return inst;
}

std::string& Logger::line_buffer() noexcept {
    thread_local std::string buffer;
    if (buffer.capacity() < 256) {
        buffer.reserve(256);
    }
    return buffer;
}

void Logger::append_timestamp_field(std::string& out) {
    out += "\"ts\":\"";
    append_iso8601_utc(out);
    out.push_back('"');
}

void Logger::append_level_field(std::string& out, Level level) {
    out += ",\"level\":";
    json::append_string_literal(out, to_string(level));
}

void Logger::append_event_field(std::string& out, std::string_view event) {
    out += ",\"event\":";
    json::append_string_literal(out, event);
}

void Logger::append_callsite_fields(std::string& out, std::source_location const& location) {
    out += ",\"file\":";
    json::append_string_literal(out, basename_of(location.file_name()));
    out += ",\"line\":";
    append_decimal(out, static_cast<std::uint32_t>(location.line()));
}

void Logger::append_field_key(std::string& out, std::string_view key) {
    json::append_string_literal(out, key);
}

void Logger::append_null(std::string& out) {
    json::append_null(out);
}

void Logger::append_bool(std::string& out, bool value) {
    json::append_bool(out, value);
}

void Logger::append_number(std::string& out, std::int64_t value) {
    json::append_number(out, value);
}

// json::append_number는 int64만 받아 u64 max에서 overflow한다.
// uint64는 직접 십진수로 출력한다.
void Logger::append_number(std::string& out, std::uint64_t value) {
    append_decimal(out, value);
}

void Logger::append_number(std::string& out, double value) {
    json::append_number(out, value);
}

void Logger::append_string_literal(std::string& out, std::string_view value) {
    json::append_string_literal(out, value);
}

} // namespace ddcs::logger
