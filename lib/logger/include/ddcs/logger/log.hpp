#pragma once

#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

#include <cstdint>

namespace ddcs::logger {

enum class Level : std::uint8_t {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

// 문자열 -> Level (대소문자 무시): "debug"/"info"/"warn"|"warning"/"error".
// 미매칭이면 fallback. (env 기반 로그레벨 설정 등에 사용.)
Level level_from_string(std::string_view name, Level fallback) noexcept;

// 한 줄 출력 sink. 한 호출 = 한 로그 레코드 = 한 JSON 객체.
// 줄바꿈은 호출자(Logger)가 line 안에 포함시키지 않음 - sink 가 책임.
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(std::string_view line) noexcept = 0;
};

// stdout 으로 즉시 출력. Warn 이상만 flush.
class StdoutSink : public Sink {
public:
    void write(std::string_view line) noexcept override;
    void flush() noexcept;
};

// 가변 kv 의 값 타입.
template <typename T>
struct Field {
    std::string_view key;
    T value;
};

template <typename T>
constexpr Field<std::decay_t<T>> kv(std::string_view k, T&& v) noexcept {
    return Field<std::decay_t<T>>{k, std::forward<T>(v)};
}

namespace detail {

// 각 value 타입을 JSON 으로 직렬화. buf 끝에 추기.
void append_value(std::string& buf, std::string_view v);
void append_value(std::string& buf, bool v);
void append_value(std::string& buf, std::nullptr_t);
void append_value(std::string& buf, double v);
void append_value(std::string& buf, std::int64_t v);
void append_value(std::string& buf, std::uint64_t v);

inline void append_value(std::string& buf, std::string const& v) { append_value(buf, std::string_view{v}); }
inline void append_value(std::string& buf, char const* v) {
    append_value(buf, std::string_view{v == nullptr ? "" : v});
}

template <typename T>
    requires std::is_integral_v<T> && (!std::is_same_v<T, bool>) && (!std::is_same_v<T, char>)
inline void append_value(std::string& buf, T v) {
    if constexpr (std::is_signed_v<T>) {
        append_value(buf, static_cast<std::int64_t>(v));
    } else {
        append_value(buf, static_cast<std::uint64_t>(v));
    }
}

template <typename T>
    requires std::is_enum_v<T>
inline void append_value(std::string& buf, T v) {
    append_value(buf, static_cast<std::underlying_type_t<T>>(v));
}

template <typename T>
inline void append_field(std::string& buf, Field<T> const& f) {
    buf.push_back(',');
    buf.push_back('"');
    buf.append(f.key);
    buf.append("\":", 2);
    append_value(buf, f.value);
}

void build_header(std::string& buf, Level lvl, std::source_location const& loc, std::string_view msg);
void finish_line(std::string& buf);

std::string& thread_buffer() noexcept;

} // namespace detail

class Logger {
public:
    static Logger& instance() noexcept;

    void set_level(Level lvl) noexcept { threshold_ = lvl; }
    void set_sink(Sink& sink) noexcept { sink_ = &sink; }

    bool enabled(Level lvl) const noexcept {
        return static_cast<std::uint8_t>(lvl) >= static_cast<std::uint8_t>(threshold_);
    }

    template <typename... Fs>
    void log(Level lvl, std::source_location const& loc, std::string_view msg, Fs const&... fields) noexcept {
        if (sink_ == nullptr) [[unlikely]] {
            return;
        }
        std::string& buf = detail::thread_buffer();
        buf.clear();
        detail::build_header(buf, lvl, loc, msg);
        (detail::append_field(buf, fields), ...);
        detail::finish_line(buf);
        sink_->write(buf);
    }

private:
    Logger() = default;
    Level threshold_{Level::Info};
    Sink* sink_{nullptr};
};

} // namespace ddcs::logger

// 매크로: level guard early exit - disabled 레벨에서 args 평가 안 함.
#define DDCS_LOG_IMPL(_lvl, _msg, ...)                                                                                 \
    do {                                                                                                               \
        auto& _ddcs_logger = ::ddcs::logger::Logger::instance();                                                       \
        if (_ddcs_logger.enabled(_lvl)) {                                                                              \
            _ddcs_logger.log(_lvl, ::std::source_location::current(), _msg __VA_OPT__(, ) __VA_ARGS__);                \
        }                                                                                                              \
    } while (0)

#define LOG_DEBUG(msg, ...) DDCS_LOG_IMPL(::ddcs::logger::Level::Debug, msg __VA_OPT__(, ) __VA_ARGS__)
#define LOG_INFO(msg, ...) DDCS_LOG_IMPL(::ddcs::logger::Level::Info, msg __VA_OPT__(, ) __VA_ARGS__)
#define LOG_WARN(msg, ...) DDCS_LOG_IMPL(::ddcs::logger::Level::Warn, msg __VA_OPT__(, ) __VA_ARGS__)
#define LOG_ERROR(msg, ...) DDCS_LOG_IMPL(::ddcs::logger::Level::Error, msg __VA_OPT__(, ) __VA_ARGS__)
