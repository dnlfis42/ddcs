#pragma once

#include "ddcs/io/sys_result.hpp"

#include <stdexcept>
#include <string>
#include <system_error>

namespace ddcs::io {

[[noreturn]] inline void throw_errno(int err, char const* context) {
    throw std::system_error{err, std::system_category(), context};
}

// SysResult를 예외로 바꾼다. 조립 루트가 부팅 단계마다 쓴다.
// err == 0(errno 없는 실패)은 init/start 순서 위반뿐인 프로그래머 오류라,
// 환경 실패 문구로 오독되지 않게 구분해 던진다.
[[noreturn]] inline void throw_boot_failure(SysResult result, std::string const& context) {
    if (result.err != 0) {
        throw_errno(result.err, context.c_str());
    }
    throw std::logic_error{context + " failed (boot sequence error)"};
}

} // namespace ddcs::io
