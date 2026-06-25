#pragma once

#include "ddcs/json/value.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ddcs::config {

// 레이어드 JSON config 로더
// - add_file/add_text 순서대로 layer를 쌓고, 조회는 나중에 추가된 layer 우선
// - 각 getter는 우선순위: env(있으면) > layer(dotted path) > fallback
// - dotted path: "timeout_ms.handshake"처럼 중첩 object를 점으로 탐색
// - 시간 값은 정수 ms로 저장하고 chrono로 반환한다.
//
// 에러 정책:
// - 파일 없음 -> add_file이 false 반환 (호출부가 경고)
// - JSON malformed -> std::runtime_error
// - 키 없음 -> fallback (조용히)
// - 값 타입 불일치 -> fallback + stderr 경고
class Config {
public:
    // path의 JSON을 layer로 추가한다. 파일이 없으면 false(기본값 동작). 파싱 실패면 throw.
    bool add_file(std::filesystem::path const& path);

    // JSON 텍스트를 layer로 추가한다(테스트/인메모리용). 파싱 실패면 throw.
    void add_text(std::string_view text);

    [[nodiscard]] std::string
    get_string(std::string_view path, char const* env, std::string_view fallback) const;
    [[nodiscard]] std::uint16_t
    get_port(std::string_view path, char const* env, std::uint16_t fallback) const;
    [[nodiscard]] int get_int(std::string_view path, char const* env, int fallback) const;
    // ms 정수를 읽어 chrono로 변환한다(시간 키는 env override 없음).
    [[nodiscard]] std::chrono::nanoseconds
    get_duration_ms(std::string_view path, std::int64_t fallback_ms) const;

private:
    // dotted path를 layer들에서 조회한다(나중 layer 우선). 없으면 nullptr.
    [[nodiscard]] ddcs::json::Value const* lookup(std::string_view path) const;

    std::vector<ddcs::json::Value> layers_;
};

} // namespace ddcs::config
