#include "ddcs/config/config.hpp"

#include "ddcs/common/env.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ddcs::config {

namespace {

// dotted path("a.b.c")를 root에서 따라간다. 없거나 중간이 object가 아니면 nullptr.
[[nodiscard]] ddcs::json::Value const*
navigate(ddcs::json::Value const& root, std::string_view path) {
    ddcs::json::Value const* cur = &root;
    std::size_t start = 0;
    for (;;) {
        std::size_t const dot = path.find('.', start);
        std::string_view const seg =
            (dot == std::string_view::npos) ? path.substr(start) : path.substr(start, dot - start);
        cur = cur->find(seg);
        if (cur == nullptr) {
            return nullptr;
        }
        if (dot == std::string_view::npos) {
            return cur;
        }
        start = dot + 1;
    }
}

[[nodiscard]] char const* env_value(char const* env) {
    return env != nullptr ? std::getenv(env) : nullptr;
}

void warn_type(std::string_view path, char const* expected) {
    std::fprintf(
        stderr, "ddcs config: key '%.*s' is not %s; using default\n", static_cast<int>(path.size()),
        path.data(), expected
    );
}

} // namespace

void Config::add_text(std::string_view text) {
    auto parsed = ddcs::json::parse(text);
    if (!parsed) {
        throw std::runtime_error{"config: malformed JSON"};
    }
    layers_.push_back(std::move(*parsed));
}

bool Config::add_file(std::filesystem::path const& path) {
    std::ifstream file{path};
    if (!file) {
        return false; // 파일 없음 -> 기본값으로 동작 (호출부에서 경고)
    }
    std::string const text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    try {
        add_text(text);
    } catch (std::runtime_error const&) {
        throw std::runtime_error{"config: malformed JSON in " + path.string()};
    }
    return true;
}

ddcs::json::Value const* Config::lookup(std::string_view path) const {
    for (auto it = layers_.rbegin(); it != layers_.rend(); ++it) { // 나중 layer 우선
        if (auto const* v = navigate(*it, path)) {
            return v;
        }
    }
    return nullptr;
}

std::string
Config::get_string(std::string_view path, char const* env, std::string_view fallback) const {
    if (char const* e = env_value(env)) {
        return std::string{e};
    }
    if (auto const* v = lookup(path)) {
        if (auto const s = v->as_string()) {
            return std::string{*s};
        }
        warn_type(path, "a string");
    }
    return std::string{fallback};
}

std::uint16_t
Config::get_port(std::string_view path, char const* env, std::uint16_t fallback) const {
    if (char const* e = env_value(env)) {
        if (auto const p = ddcs::common::parse_port(e)) {
            return *p;
        }
        std::fprintf(stderr, "ddcs config: env %s is not a valid port; ignoring\n", env);
        // 무효 env는 layer/default로 흘려보낸다(env 미설정과 동일 취급)
    }
    if (auto const* v = lookup(path)) {
        if (auto const n = v->as_int64(); n && *n >= 1 && *n <= 65535) {
            return static_cast<std::uint16_t>(*n);
        }
        warn_type(path, "a port (1..65535)");
    }
    return fallback;
}

int Config::get_int(std::string_view path, char const* env, int fallback) const {
    if (char const* e = env_value(env)) {
        std::string_view const ev{e};
        int value = 0;
        auto const [ptr, ec] = std::from_chars(ev.data(), ev.data() + ev.size(), value);
        if (ec == std::errc{} && ptr == ev.data() + ev.size()) {
            return value;
        }
        std::fprintf(stderr, "ddcs config: env %s is not an integer; ignoring\n", env);
    }
    if (auto const* v = lookup(path)) {
        if (auto const n = v->as_int64()) {
            return static_cast<int>(*n);
        }
        warn_type(path, "an integer");
    }
    return fallback;
}

std::chrono::nanoseconds
Config::get_duration_ms(std::string_view path, std::int64_t fallback_ms) const {
    std::int64_t ms = fallback_ms;
    if (auto const* v = lookup(path)) {
        if (auto const n = v->as_int64()) {
            ms = *n;
        } else {
            warn_type(path, "an integer (ms)");
        }
    }
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{ms});
}

} // namespace ddcs::config
