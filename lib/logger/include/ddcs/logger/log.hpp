#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ddcs::logger {

enum class Level : std::uint8_t {
    debug = 0,
    info = 1,
    warn = 2,
    error = 3,
};

constexpr std::string_view to_string(Level level) noexcept {
    switch (level) {
    case Level::debug:
        return "DEBUG";
    case Level::info:
        return "INFO";
    case Level::warn:
        return "WARN";
    case Level::error:
        return "ERROR";
    }
    return "UNKNOWN";
}

// 대소문자를 무시하고 매칭한다.
// "warning"은 Warn의 alias
// 알 수 없는 이름이면 nullopt
std::optional<Level> parse_level(std::string_view text) noexcept;

template <typename T>
struct Field {
    std::string_view key;
    T value;
};

template <typename T>
constexpr Field<std::decay_t<T>> kv(std::string_view key, T&& value) noexcept {
    return Field<std::decay_t<T>>{key, std::forward<T>(value)};
}

class Sink {
public:
    virtual ~Sink() = default;

    virtual void write(std::string_view line) noexcept = 0;
};

class StdoutSink : public Sink {
public:
    void write(std::string_view line) noexcept override;
    void flush() noexcept;
};

class Logger {
public:
    static Logger& instance() noexcept;

    Level level() {
        return threshold_;
    }

    bool enabled(Level level) const noexcept {
        return static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(threshold_);
    }

    void set_level(Level level) noexcept {
        threshold_ = level;
    }

    void set_sink(Sink& sink) noexcept {
        sink_ = &sink;
    }

    // 자신이 심은 sink를 떼어낸다.
    // sink 소유자는 sink 파괴 전에 이걸 호출해 dangling을 막는다.
    void clear_sink(Sink const& expected) noexcept {
        if (sink_ == &expected) {
            sink_ = nullptr;
        }
    }

    // - 필드 순서 고정: ts, level, event, 사용자 fields, file, line (file/line은 마지막 metadata)
    // - sink가 없으면 no-op
    template <typename... Fields>
    void
    log(Level level, std::source_location const& location, std::string_view event,
        Fields const&... fields) noexcept {
        if (sink_ == nullptr) [[unlikely]] {
            return;
        }

        std::string& line = line_buffer();
        line.clear();

        line.push_back('{');
        append_timestamp_field(line);
        append_level_field(line, level);
        append_event_field(line, event);
        (append_field(line, fields), ...);
        append_callsite_fields(line, location);
        line.push_back('}');

        sink_->write(line);
    }

private:
    Logger() = default;

    static std::string& line_buffer() noexcept;

    static void append_timestamp_field(std::string& out);
    static void append_level_field(std::string& out, Level level);
    static void append_event_field(std::string& out, std::string_view event);
    static void append_callsite_fields(std::string& out, std::source_location const& location);

    template <typename T>
    static void append_field(std::string& out, Field<T> const& field) {
        out.push_back(',');
        append_field_key(out, field.key);
        out.push_back(':');
        append_field_value(out, field.value);
    }

    static void append_field_key(std::string& out, std::string_view key);
    template <typename T>
    static void append_field_value(std::string& out, T const& value);

    template <typename T>
    static constexpr bool unsupported_field_value = false;

    static void append_null(std::string& out);
    static void append_bool(std::string& out, bool value);
    static void append_number(std::string& out, std::int64_t value);
    static void append_number(std::string& out, std::uint64_t value);
    static void append_number(std::string& out, double value);
    static void append_string_literal(std::string& out, std::string_view value);

    Level threshold_ = Level::info;
    Sink* sink_ = nullptr;
};

template <typename T>
void Logger::append_field_value(std::string& out, T const& value) {
    using Value = std::remove_cvref_t<T>;

    if constexpr (std::is_same_v<Value, std::nullptr_t>) {
        append_null(out);
    } else if constexpr (std::is_same_v<Value, bool>) {
        append_bool(out, value);
    } else if constexpr (std::is_same_v<Value, std::string>) {
        append_string_literal(out, std::string_view{value});
    } else if constexpr (std::is_same_v<Value, std::string_view>) {
        append_string_literal(out, value);
    } else if constexpr (
        std::is_pointer_v<Value> &&
        std::is_same_v<std::remove_cv_t<std::remove_pointer_t<Value>>, char>
    ) {
        append_string_literal(out, std::string_view{value == nullptr ? "" : value});
    } else if constexpr (std::is_enum_v<Value>) {
        append_field_value(out, static_cast<std::underlying_type_t<Value>>(value));
    } else if constexpr (
        std::is_integral_v<Value> && (!std::is_same_v<Value, char>) &&
        (!std::is_same_v<Value, bool>)
    ) {
        if constexpr (std::is_signed_v<Value>) {
            append_number(out, static_cast<std::int64_t>(value));
        } else {
            append_number(out, static_cast<std::uint64_t>(value));
        }
    } else if constexpr (std::is_floating_point_v<Value>) {
        append_number(out, static_cast<double>(value));
    } else {
        static_assert(
            unsupported_field_value<Value>,
            "Logger: field value type must be null, bool, number, string, or enum"
        );
    }
}

} // namespace ddcs::logger

// disabled 레벨에서는 args를 평가하지 않는다.
#define DDCS_LOG_IMPL(_level, _event, ...)                                                         \
    do {                                                                                           \
        auto& _ddcs_logger = ::ddcs::logger::Logger::instance();                                   \
        if (_ddcs_logger.enabled(_level)) {                                                        \
            _ddcs_logger.log(                                                                      \
                _level, ::std::source_location::current(), _event __VA_OPT__(, ) __VA_ARGS__       \
            );                                                                                     \
        }                                                                                          \
    } while (0)

#define LOG_DEBUG(event, ...)                                                                      \
    DDCS_LOG_IMPL(::ddcs::logger::Level::debug, event __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INFO(event, ...)                                                                       \
    DDCS_LOG_IMPL(::ddcs::logger::Level::info, event __VA_OPT__(, ) __VA_ARGS__)
#define LOG_WARN(event, ...)                                                                       \
    DDCS_LOG_IMPL(::ddcs::logger::Level::warn, event __VA_OPT__(, ) __VA_ARGS__)
#define LOG_ERROR(event, ...)                                                                      \
    DDCS_LOG_IMPL(::ddcs::logger::Level::error, event __VA_OPT__(, ) __VA_ARGS__)
