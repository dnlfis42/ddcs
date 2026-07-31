#pragma once

#include "ddcs/json/value.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ddcs::config::file {

// 설정 파일을 읽어 파싱한다. 파일 없음(ENOENT)이면 nullopt(기본값 동작).
// 그 외 열기/읽기 실패와 malformed JSON이면 throw
[[nodiscard]] std::optional<json::Value> load(std::filesystem::path const& path);

[[nodiscard]] std::string
get_string(json::Value const& root, std::string_view path, std::string_view fallback);

// int 범위 검사 포함(무언 내로잉 방지)
[[nodiscard]] int get_int(json::Value const& root, std::string_view path, int fallback);

// 1..65535 검사 포함
[[nodiscard]] std::uint16_t
get_port(json::Value const& root, std::string_view path, std::uint16_t fallback);

// 양수 byte 크기. 0 이하를 size_t로 캐스팅하면 거대한 값으로 감기므로 여기서 막는다
[[nodiscard]] std::size_t
get_size(json::Value const& root, std::string_view path, std::size_t fallback, std::size_t max);

// ms 정수를 chrono로. 음수는 거부
[[nodiscard]] std::chrono::nanoseconds
get_duration_ms(json::Value const& root, std::string_view path, std::chrono::nanoseconds fallback);

} // namespace ddcs::config::file
