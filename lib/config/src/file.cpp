#include "ddcs/config/file.hpp"

#include "ddcs/logger/event.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace ddcs::config::file {

std::optional<json::Value> load(std::filesystem::path const& path) {
    errno = 0;
    std::ifstream in{path};
    if (!in) {
        if (errno == ENOENT || errno == ENOTDIR) {
            return std::nullopt; // 경로 부재 계열 -> 기본값으로 동작 (호출부에서 경고)
        }
        // 권한 오류 등을 '파일 없음'으로 뭉개면 기본값으로 조용히 부팅하는 함정이 된다.
        // errno는 문자열로 풀지 않고 system_error에 실어, io 계층의 throw_errno와 같은 모양으로
        // 남긴다(std::strerror는 스레드 안전도 아니다).
        throw std::system_error{
            errno, std::system_category(), "config: cannot open " + path.string()
        };
    }

    // istreambuf_iterator는 스트림 상태 비트를 안 세우고 filebuf가 직접 throw하므로
    // catch로 잡아 진단을 입힌다. 잘린 텍스트가 parse로 흘러가면 malformed로 오보고된다.
    std::string text;
    try {
        text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
    } catch (std::ios_base::failure const& e) {
        // 잡은 원인을 그대로 얹는다. 버리면 진단을 입히려고 잡은 의미가 없어진다.
        throw std::runtime_error{"config: read error in " + path.string() + ": " + e.what()};
    }

    auto parsed = json::parse(text);
    if (!parsed) {
        throw std::runtime_error{"config: malformed JSON in " + path.string()};
    }
    return parsed;
}

std::string get_string(json::Value const& root, std::string_view path, std::string_view fallback) {
    if (auto const* v = root.find_path(path)) {
        if (auto const s = v->as_string()) {
            return std::string{*s};
        }
        LOG_CONFIG_VALUE_INVALID("file", path, "string", v->dump());
    }
    return std::string{fallback};
}

int get_int(json::Value const& root, std::string_view path, int fallback) {
    if (auto const* v = root.find_path(path)) {
        // int 범위 밖도 거부한다(무언 내로잉 왜곡 방지)
        if (auto const n = v->as_int64();
            n && *n >= std::numeric_limits<int>::min() && *n <= std::numeric_limits<int>::max()) {
            return static_cast<int>(*n);
        }
        LOG_CONFIG_VALUE_INVALID("file", path, "integer", v->dump());
    }
    return fallback;
}

std::uint16_t get_port(json::Value const& root, std::string_view path, std::uint16_t fallback) {
    if (auto const* v = root.find_path(path)) {
        if (auto const n = v->as_int64(); n && *n >= 1 && *n <= 65535) {
            return static_cast<std::uint16_t>(*n);
        }
        LOG_CONFIG_VALUE_INVALID("file", path, "port (1..65535)", v->dump());
    }
    return fallback;
}

std::size_t
get_size(json::Value const& root, std::string_view path, std::size_t fallback, std::size_t max) {
    if (auto const* v = root.find_path(path)) {
        if (auto const n = v->as_int64(); n && *n > 0 && static_cast<std::uint64_t>(*n) <= max) {
            return static_cast<std::size_t>(*n);
        }
        // 상한이 없으면 오타 하나가 bad_alloc 한 줄로 죽어 어느 키 탓인지 남지 않는다.
        std::string const expected = "positive integer (1.." + std::to_string(max) + ")";
        LOG_CONFIG_VALUE_INVALID("file", path, expected, v->dump());
    }
    return fallback;
}

std::chrono::nanoseconds
get_duration_ms(json::Value const& root, std::string_view path, std::chrono::nanoseconds fallback) {
    if (auto const* v = root.find_path(path)) {
        // ns 변환(x1'000'000)이 int64를 넘지 않는 상한까지만 받는다 (초과는 signed overflow UB)
        constexpr std::int64_t max_ms = std::numeric_limits<std::int64_t>::max() / 1'000'000;
        if (auto const n = v->as_int64(); n && *n >= 0 && *n <= max_ms) {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::milliseconds{*n
            });
        }
        // 음수/과대 ms가 시한 값으로 흘러가면 timeout 산술이 전부 무의미해진다
        LOG_CONFIG_VALUE_INVALID("file", path, "non-negative integer", v->dump());
    }
    return fallback;
}

} // namespace ddcs::config::file
